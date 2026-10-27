#include "Utils/DebugGate.hpp"
// Native ink paint/primitive ownership, verified in world-markers-20260923.md.
#include "Stereo/WorldMarkers.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Render/WorldMarkerProjection.hpp"
#include "Render/WorldMarkerScale.hpp"
#include "Render/WorldMarkerOwnership.hpp"
#include "Render/WorldMarkerShader.hpp"
#include "Camera/CameraLink.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/Trampoline.hpp"
#include "Runtimes/AsymmetricProjection.hpp"
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Utils.hpp>
#include <RED4ext/Scripting/Natives/inkWidget.hpp>
#include <RED4ext/Scripting/Natives/inkIWidgetLogicController.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/WidgetLogicController.hpp>
#include <RED4ext/Scripting/Natives/inkLayer.hpp>
#include <RED4ext/Scripting/Natives/inkLayerProxy.hpp>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

extern void Log(const char*,...);
extern "C" float CyberpunkVRPort_HalfIpd();
extern "C" float GetGameRenderFovDeg();
extern "C" float CyberpunkVR_MainAdsZoomFactor;
extern "C" int CyberpunkVR_MainIsRightEye;
extern "C" int CyberpunkVR_StereoSubmit;
extern "C" void CvrWorldMarkerTextPublishDetour();
extern "C" int GetMenuMode();
extern int g_launcherWidth;
extern int g_launcherHeight;
extern "C" {
__declspec(dllexport) std::atomic<int32_t> CyberpunkVR_WorldMarkerStereo{1};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerRoots{0};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerQuads{0};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerTexts{0};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerCameraUploads[2]{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerTextUploads[2]{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerMappinStages[7]{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_WorldMarkerProjectionStages[5]{};
// 1 = under the asymmetric projection, move each view's flat HUD by that view's own projection shift
// (see InkShift). 0 = the HUD composed at the same pixels in both views, as before.
__declspec(dllexport) std::atomic<int32_t> CyberpunkVR_AsymHudShift{1};
}

namespace cvr::markers {
namespace {
using namespace RED4ext;
const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
std::atomic<bool> hooksReady{false};
bool Enabled() {
    return hooksReady.load(std::memory_order_acquire) && VertexShadersReady() &&
        CyberpunkVR_WorldMarkerStereo.load(std::memory_order_relaxed) && CyberpunkVR_StereoSubmit;
}
template<class T> T& At(void* p,size_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+offset); }
struct RootDepth { WeakHandle<ink::Widget> root; float inverseDepth{}; uint64_t stamp{}; MarkerScale scale; bool inheritDepth{}; };
std::shared_mutex rootsMutex;
std::unordered_map<ink::Widget*,RootDepth> roots;
struct ProjectionOwner { WeakHandle<IScriptable> projection; WeakHandle<ink::Widget> widget; uint64_t stamp{}; };
std::mutex projectionMutex;
std::unordered_map<void*,ProjectionOwner> projectionOwners;
thread_local float paintDepth=0;
thread_local uintptr_t paintOwner=0;
thread_local unsigned offscreenPass=0;
thread_local float textDepth=0;

bool HudRoot(const Handle<ink::Widget>& root) {
    auto current=root;
    for(int i=0;current && i<24;++i) {
        const auto proxy=current->layerProxy;
        const auto layer=proxy ? proxy->layer.Lock() : Handle<ink::Layer>{};
        if(layer) return layer->GetType()->GetName()==CName("inkHUDLayer");
        current=current->parentWidget.Lock();
    }
    return false;
}
bool WorldMappinController(IScriptable* controller) {
    // The world container's game controller is NOT Widget::logicController:
    // the live Root/HUDMiddleWidget/window ancestor chain has no logic controller.
    // This call site is already MappinBaseController; classify the actual caller.
    auto type=controller->GetType();
    for(int i=0;type && i<64;++i,type=type->parent) {
        const auto name=type->GetName();
        if(name==CName("gameuiBaseMinimapMappinController") ||
           name==CName("gameuiBaseWorldMapMappinController")) return false;
        if(name==CName("gameuiMappinBaseController")) return true;
    }
    return false;
}
void Remember(const Handle<ink::Widget>& root,const float* world,bool valid=true,bool scaleMarker=false,bool inheritDepth=false,
    bool pinToAnchor=false) {
    if(!root || !HudRoot(root)) return;
    camera::LocatedCameraFrame frame{};
    const float depth=valid && world && camera::LocatedCameraFrameRead(&frame)
        ? ForwardDepth(world,frame.worldPos,frame.worldQuat) : 0;
    const uint64_t now=GetTickCount64();
    Vector2 target{};bool resize=false;
    {
        std::unique_lock lock(rootsMutex);
        auto& entry=roots[root.GetPtr()];
        if(entry.root.Lock().GetPtr()!=root.GetPtr())entry={WeakHandle<ink::Widget>(root)};
        entry.inverseDepth=depth>0 ? 1/depth : 0;entry.stamp=now;
        entry.inheritDepth=inheritDepth;
        if(scaleMarker)resize=entry.scale.Target(root->renderTransform.scale.X,root->renderTransform.scale.Y,target.X,target.Y);
        if(roots.size()>512) {
            for(auto it=roots.begin();it!=roots.end();) {
                // Retain scale ownership while a hidden marker is still alive;
                // expiring it by time alone would shrink it again on reappearance.
                if(now-it->second.stamp>2000 && !it->second.root.Lock())it=roots.erase(it);else ++it;
            }
        }
        CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerRoots.store(roots.size(),std::memory_order_relaxed));
    }
    // SHRINK ABOUT THE POINT ON THE TARGET. A mappin controller's own root is placed by its anchor point
    // and its content hangs from that point (quest/gameplay mappins: a 75x75 root, anchor 0,0, the
    // content canvas at 0,0), while the authored pivot is the box's centre. Scaled about the centre, the
    // icon moved (1-0.7)*37.5 units right and down, off the NPC's nameplate chevron, which is not
    // scaled. Presentation widgets keep their pivot: the game centres those on the target itself.
    if(pinToAnchor) {
        Vector2 anchor=root->layout.anchorPoint;
        if(root->renderTransformPivot.X!=anchor.X || root->renderTransformPivot.Y!=anchor.Y) {
            // Two script overloads share the short name: (pivot: Vector2) native, (x, y) scripted.
            auto pivot=root->GetType()->GetFunction("SetRenderTransformPivot");
            StackArgs_t pivotArgs;
            if(pivot && pivot->params.Size()==1 && pivot->params[0]->type->GetName()==CName("Vector2"))
                pivotArgs.emplace_back(nullptr,&anchor);
            else if(pivot && pivot->params.Size()==2) {
                pivotArgs.emplace_back(nullptr,&anchor.X);pivotArgs.emplace_back(nullptr,&anchor.Y);
            } else pivot=nullptr;
            if(pivot)ExecuteFunction(root.GetPtr(),pivot,nullptr,pivotArgs);
        }
    }
    if(resize) {
        auto setter=root->GetType()->GetFunction("SetScale");
        if(!setter || setter->params.Size()!=1 || setter->params[0]->type->GetName()!=CName("Vector2")) {
            static std::atomic<bool> warned{false};
            if(!warned.exchange(true))Log("[world-markers] SetScale(Vector2) unavailable; original marker size retained\n");
            return;
        }
        // The engine setter preserves the pivot and invalidates paint.
        // Only native world-mappin roots enter here, never map icons or chatters.
        StackArgs_t args;args.emplace_back(nullptr,&target);
        if(ExecuteFunction(root.GetPtr(),setter,nullptr,args)) {
            std::unique_lock lock(rootsMutex);
            auto it=roots.find(root.GetPtr());
            if(it!=roots.end() && it->second.root.Lock().GetPtr()==root.GetPtr())it->second.scale.Commit(target.X,target.Y);
        }
    }
}

using MappinPositionFn=uintptr_t (*)(void*,void*);
MappinPositionFn originalMappinPosition{};
uintptr_t MappinPosition(void* controller,void* position) {
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[0].fetch_add(1,std::memory_order_relaxed));
    const auto result=originalMappinPosition(controller,position);
    if(!Enabled()) return result;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[1].fetch_add(1,std::memory_order_relaxed));
    // This setter belongs to MappinBaseController. GetMappin's native thunk
    // copies exactly its weak handle at A0; the root is the SDK's weak handle40.
    // Follow the native setter's selected target, including the separate
    // presentation widget used by cached/attached mappins.
    const bool presentation=At<uint8_t>(controller,0x190)!=0;
    auto root=At<WeakHandle<ink::Widget>>(controller,presentation?0x170:0x40).Lock();
    if(!root) return result;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[2].fetch_add(1,std::memory_order_relaxed));
    if(!WorldMappinController(static_cast<IScriptable*>(controller))) return result;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[3].fetch_add(1,std::memory_order_relaxed));
    if(!HudRoot(root)) return result;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[4].fetch_add(1,std::memory_order_relaxed));
    // Visibility can be assigned later in this same native update. Paint itself
    // gates hidden widgets; collecting the anchor must not depend on that order.
    const auto vt=*static_cast<uintptr_t**>(controller);
    const bool clamped=reinterpret_cast<bool (*)(void*)>(vt[0x158/8])(controller);
    auto mappin=At<WeakHandle<IScriptable>>(controller,0xA0).Lock();
    Vector4 world{};
    const auto getter=mappin ? mappin->GetType()->GetFunction("GetWorldPosition") : nullptr;
    // THE POINT IS KEPT EVEN WHEN THIS CONTROLLER DOES NOT PROJECT (+0xE3, SetProjectToScreenSpace off).
    // Such a root can still be drawn where the world point is: two quest mappins on one looted body,
    // one of them with the flag off, both visible at the same spot -- that one, given no depth, sat at
    // infinity and split from the other by the full parallax in the second view. A clamped edge icon
    // is not at its world point and keeps no depth.
    const bool valid=!clamped && getter && ExecuteFunction(mappin.GetPtr(),getter,&world);
    const float p[]={world.X,world.Y,world.Z};
    // Stealth icons can be reparented into the NPC nameplate. In that mode
    // they inherit its projection instead of masking the parent depth with 0;
    // with no projected parent to inherit from they take their own point (Paint).
    Remember(root,p,valid,true,!At<uint8_t>(controller,0xE3),!presentation);
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[5].fetch_add(1,std::memory_order_relaxed));
    if(valid) CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerMappinStages[6].fetch_add(1,std::memory_order_relaxed));
    return result;
}

using ProjectionFn=void (*)(void*,void*,void*,float);
ProjectionFn originalProjection{};
void Projection(void* projection,void* view,void* size,float worldScale) {
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerProjectionStages[0].fetch_add(1,std::memory_order_relaxed));
    originalProjection(projection,view,size,worldScale);
    if(!Enabled()) return;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerProjectionStages[1].fetch_add(1,std::memory_order_relaxed));
    Handle<ink::Widget> root;
    {
        std::lock_guard lock(projectionMutex);
        const auto it=projectionOwners.find(projection);
        if(it!=projectionOwners.end() && it->second.projection.Lock().GetPtr()==projection && GetTickCount64()-it->second.stamp<1000)
            root=it->second.widget.Lock();
    }
    if(!root) {
        auto owner=At<WeakHandle<IScriptable>>(projection,0xD0).Lock();
        const auto logicClass=CRTTISystem::Get()->GetClass("inkIWidgetLogicController");
        if(!owner || !logicClass || !owner->GetType()->IsA(logicClass)) return;
        root=At<WeakHandle<ink::Widget>>(owner.GetPtr(),0x40).Lock();
    }
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerProjectionStages[2].fetch_add(1,std::memory_order_relaxed));
    if(!root || !HudRoot(root)) return;
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerProjectionStages[3].fetch_add(1,std::memory_order_relaxed));
    float world[3]{};
    // Same native entity/slot lookup as the projection update, with its static
    // position fallback. Constants below are read from the verified executable.
    using WorldFn=bool (*)(void*,float*);
    if(!reinterpret_cast<WorldFn>(base+0x4E8FD4)(projection,world))
        std::memcpy(world,static_cast<uint8_t*>(projection)+0xB0,sizeof(world));
    const bool adjust=At<uint8_t>(projection,0xE0)!=0;
    const float minimum=*reinterpret_cast<const float*>(base+0x31EF494);
    const float scale=adjust && worldScale>minimum
        ? *reinterpret_cast<const float*>(base+0x31EF5A8)/worldScale : 1;
    for(int i=0;i<3;++i) world[i]+=At<float>(projection,0xC0+i*4)*scale;
    if(adjust) {
        const float distance=At<float>(projection,0x104)/(worldScale>minimum ? worldScale : 1);
        const float factor=std::min(distance,*reinterpret_cast<const float*>(base+0x31EF890)) *
            *reinterpret_cast<const float*>(base+0x31EEFB0);
        for(int i=0;i<3;++i) world[i]+=At<float>(projection,0xE4+i*4)*factor;
    }
    Remember(root,world);
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerProjectionStages[4].fetch_add(1,std::memory_order_relaxed));
}

using PaintFn=void (*)(ink::Widget*,void*);
PaintFn originalPaint{};
void Paint(ink::Widget* widget,void* context) {
    const float previous=paintDepth;
    const auto previousOwner=paintOwner;
    if(Enabled()) {
        std::shared_lock lock(rootsMutex);
        const auto it=roots.find(widget);
        if(it!=roots.end()) {
            const auto live=it->second.root.Lock();
            const float own=live.GetPtr()==widget && GetTickCount64()-it->second.stamp<1000
                ? it->second.inverseDepth : 0;
            // An inheriting root keeps the depth of the projected root it is painted inside (a
            // nameplate); with none around it, it is painted at its own point instead of at infinity.
            if(!it->second.inheritDepth || paintDepth<=0) {
                paintDepth=own;
                paintOwner=paintDepth>0?reinterpret_cast<uintptr_t>(widget):0;
            }
        }
    } else {paintDepth=0;paintOwner=0;}
    originalPaint(widget,context);
    paintDepth=previous;
    paintOwner=previousOwner;
}

// A pool can reallocate while collecting a frame. Keep metadata by pool/index
// and rebuild the address index on growth; never infer ownership from rectangles.
struct PrimitivePool { uintptr_t data{}; std::vector<MarkerOwner> depths; uint64_t stamp{}; };
struct PrimitiveDepthEntry { void* owner{}; MarkerOwner marker; };
std::mutex primitiveMutex;
std::unordered_map<void*,PrimitivePool> pools;
std::unordered_map<void*,PrimitiveDepthEntry> primitives;
void ForgetPoolRecords(void* owner,const PrimitivePool& pool) {
    for(size_t i=0;i<pool.depths.size();++i) {
        const auto it=primitives.find(reinterpret_cast<void*>(pool.data+i*0x130));
        if(it!=primitives.end() && it->second.owner==owner) primitives.erase(it);
    }
}
using AllocateFn=void* (*)(void*);
AllocateFn originalAllocate{};
void* Allocate(void* batch) {
    void* result=originalAllocate(batch);
    if(!result || !hooksReady.load(std::memory_order_acquire)) return result;
    void* owner=At<void*>(batch,0x28);
    const auto data=At<uintptr_t>(owner,0);
    const auto address=reinterpret_cast<uintptr_t>(result);
    if(address<data || (address-data)%0x130 || (address-data)/0x130>131072) return result;
    const size_t index=(address-data)/0x130;
    std::lock_guard lock(primitiveMutex);
    auto& pool=pools[owner];
    if(index==0 || pool.data!=data) {
        ForgetPoolRecords(owner,pool);
        if(index==0) pool.depths.clear();
        else for(size_t i=0;i<pool.depths.size();++i)
            if(pool.depths[i].depth>0) primitives[reinterpret_cast<void*>(data+i*0x130)]={owner,pool.depths[i]};
        pool.data=data;
    }
    pool.stamp=GetTickCount64();
    if(pool.depths.size()<=index) pool.depths.resize(index+1);
    pool.depths[index]={paintOwner,paintDepth};
    if(paintDepth>0) primitives[result]={owner,{paintOwner,paintDepth}}; else primitives.erase(result);
    static uint64_t nextPrune=0;
    if(pool.stamp>=nextPrune) {
        const uint64_t now=pool.stamp;nextPrune=now+10000;
        for(auto it=pools.begin();it!=pools.end();) {
            if(now-it->second.stamp>60000) {
                ForgetPoolRecords(it->first,it->second);it=pools.erase(it);
            } else ++it;
        }
    }
    return result;
}
float PrimitiveDepth(void* record) {
    if(!Enabled()) return 0;
    std::lock_guard lock(primitiveMutex);
    const auto it=primitives.find(record);
    if(it==primitives.end()) return 0;
    const auto pool=pools.find(it->second.owner);
    if(pool!=pools.end()) pool->second.stamp=GetTickCount64();
    return it->second.marker.depth;
}

MarkerOwner CompositeOwner(void* source) {
    MarkerOwner common;
    if(!source || !Enabled())return common;
    const auto count=At<uint32_t>(source,0xC);
    const auto* indices=At<uint32_t*>(source,0);
    const auto batchPool=At<void*>(source,0x10);
    if(!indices || !batchPool || count>4096)return common;
    const auto batches=At<uintptr_t>(batchPool,0);
    const auto batchCount=At<uint32_t>(batchPool,0x10);
    if(!batches || batchCount>131072)return common;
    std::lock_guard lock(primitiveMutex);
    for(uint32_t i=0;i<count;++i) {
        if(indices[i]>=batchCount)return {};
        auto* batch=reinterpret_cast<void*>(batches+size_t(indices[i])*0xB0);
        const auto records=At<uint32_t*>(batch,0x18);
        const auto size=At<uint32_t>(batch,0x30);
        if(!size)continue;
        if(!records || size>131072)return {};
        const auto owner=At<void*>(batch,0x28);
        const auto pool=pools.find(owner);
        if(pool==pools.end() || pool->second.data!=At<uintptr_t>(owner,0))return {};
        for(uint32_t j=0;j<size;++j) {
            if(records[j]>=pool->second.depths.size() || !MergeEffectOwner(common,pool->second.depths[records[j]]))return {};
        }
    }
    return common;
}
using CompositeFn=uintptr_t (*)(void*,void*,void*,void*,void*,void*,uint8_t);
CompositeFn originalComposite{};
uintptr_t Composite(void* context,void* destination,void* source,void* writer,void* texture,void* bounds,uint8_t mode) {
    const auto owner=CompositeOwner(source);
    const auto savedOwner=paintOwner;const float savedDepth=paintDepth;
    paintOwner=owner.root;paintDepth=owner.depth;
    ++offscreenPass;
    const auto result=originalComposite(context,destination,source,writer,texture,bounds,mode);
    --offscreenPass;paintOwner=savedOwner;paintDepth=savedDepth;
    return result;
}
struct VertexStream { void* geometry; uint8_t* vertices; uint32_t count; };
static_assert(offsetof(VertexStream,count)==0x10 && sizeof(VertexStream)==0x18);
using EmitFn=void (*)(void*,void*,VertexStream*);
EmitFn originalQuad{},originalTriangles{};
void Emit(EmitFn original,void* context,void* record,VertexStream* stream) {
    const auto before=stream->count;
    const float encoded=EncodeDepth(PrimitiveDepth(record));
    original(context,record,stream);
    if(!encoded || !stream->vertices || stream->count<before || stream->count-before>65536) return;
    for(uint32_t i=before;i<stream->count;++i) {
        auto& z=*reinterpret_cast<float*>(stream->vertices+size_t(i)*24+8);
        if(z==0) z=encoded;
    }
    CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerQuads.fetch_add(1,std::memory_order_relaxed));
}
void Quad(void* context,void* record,VertexStream* stream) { Emit(originalQuad,context,record,stream); }
void Triangles(void* context,void* record,VertexStream* stream) { Emit(originalTriangles,context,record,stream); }
using TextFn=void (*)(void*,void*);
TextFn originalText{};
void Text(void* record,void* context) {
    const float previous=textDepth;
    textDepth=PrimitiveDepth(record);
    originalText(record,context);
    textDepth=previous;
}

// THE FLAT HUD UNDER THE ASYMMETRIC PROJECTION. Each eye renders its own off-centre window
// (Runtimes/AsymmetricProjection.hpp), so straight ahead is not the middle of either picture: it sits at
// the view's NDC shift. The engine knows nothing of that on the HUD side -- markers are projected with
// the game camera's centred frustum and the HUD canvas is composed over each view pixel for pixel.
// Measured on the Quest 3 profile (build/optics-20261011/asym_hud.py): a mappin landed on the same
// pixel in both eyes, exactly where the centred frustum puts it, while its target sat 116 px to either
// side. Adding the view's own shift to the canvas camera's translation fixes both kinds of element at
// once: a projected marker lands on its target, and a screen-fixed one (crosshair, prompts) sits
// straight ahead in each eye instead of at the middle of an off-centre picture.
//
// AND THE CANVAS IS SCALED TO THE WINDOW'S ASPECT. The window's aspect is the headset's (Quest 3:
// 0.9407), the canvas's is the game resolution's (2560x2848: 0.8989), and the game camera that
// places markers works at the latter -- measured: markers 4.5% too far from the centre, exactly the
// ratio. Squeezing the canvas horizontally by canvas aspect / window aspect puts them back, and gives
// screen-fixed elements square pixels again (the world is drawn into the same non-square target).
//
// Only the canvas camera, recognised by the eye's aspect: the separately drawn HUD slots (health,
// minimap, ... the free-look panel's sources) have their own cameras of their own sizes and must stay
// where they are in their textures. Never in menus.
bool InkShift(int side,float width,float height,cvr::projection::ViewWindow& window,float& squeeze) {
    if(!CyberpunkVR_AsymHudShift.load(std::memory_order_relaxed) || GetMenuMode() || g_launcherHeight<=0 || !(height>0))return false;
    const float eye=float(g_launcherWidth)/float(g_launcherHeight);
    if(std::fabs(width/height-eye)>.01f*eye || !cvr::projection::ViewWindowOf(side,&window))return false;
    squeeze=(width/height)/(window.halfX/window.halfY);
    return std::isfinite(squeeze) && squeeze>.5f && squeeze<2;
}

struct InkCamera { float width{},height{}; uint64_t stamp{}; };
thread_local InkCamera inkCameras[2];
// The second eye's marker parallax, in canvas NDC per inverse metre (the camera's x scale multiplies
// it into view NDC, which is why the asymmetric case divides by the squeeze).
float EyeScale(float width,float height) {
    const float zoom=CyberpunkVR_MainAdsZoomFactor;
    const float z=std::isfinite(zoom) && zoom>.05f ? zoom : 1;
    cvr::projection::ViewWindow window{};float squeeze=1;
    if(InkShift(1,width,height,window,squeeze)) {
        // The second view's own window: ipd / distance radians, over its horizontal half span.
        const float ipd=2*CyberpunkVRPort_HalfIpd();
        if(!std::isfinite(ipd) || ipd<=0 || ipd>.5f)return 0;
        const float scale=ipd*z/window.halfX/squeeze;
        return CyberpunkVR_MainIsRightEye!=0 ? scale : -scale;
    }
    return StereoScale(2*CyberpunkVRPort_HalfIpd(),GetGameRenderFovDeg(),width/height,z,CyberpunkVR_MainIsRightEye!=0);
}

bool InstallTextCall() {
    auto* site=reinterpret_cast<uint8_t*>(base+0x2F3D58);
    const uint8_t expected[]={0x48,0x8d,0x8b,0xf8,0x01,0,0,0xf3,0x0f,0x10,0x46,0x6c};
    if(std::memcmp(site,expected,sizeof(expected))) return false;
    auto* relay=static_cast<uint8_t*>(AllocateTrampoline(site,14));
    if(!relay) return false;
    const uint8_t jump[]={0xff,0x25,0,0,0,0};
    std::memcpy(relay,jump,sizeof(jump));
    const auto target=reinterpret_cast<uintptr_t>(&CvrWorldMarkerTextPublishDetour);
    std::memcpy(relay+6,&target,sizeof(target));
    const auto distance=reinterpret_cast<intptr_t>(relay)-reinterpret_cast<intptr_t>(site+7);
    if(distance<INT32_MIN || distance>INT32_MAX) return false;
    DWORD previous{};
    if(!VirtualProtect(site,7,PAGE_EXECUTE_READWRITE,&previous)) return false;
    // Replace the seven-byte LEA with NOP/NOP/CALL. A real return address lets
    // Windows unwind through the thunk back into the native function's frame.
    // The thunk performs the original LEA before returning to site+7.
    site[0]=site[1]=0x90;site[2]=0xe8;
    const auto relative=static_cast<int32_t>(distance);
    std::memcpy(site+3,&relative,4);
    DWORD unused{};VirtualProtect(site,7,previous,&unused);
    FlushInstructionCache(GetCurrentProcess(),relay,14);
    FlushInstructionCache(GetCurrentProcess(),site,7);
    return true;
}

bool Install() {
    struct Hook { uintptr_t rva; const char* bytes; size_t size; void* detour; void** original; };
    const Hook hooks[]={
        {0x2E37AC,"\x48\x89\x5c\x24\x10\x48\x89\x7c\x24\x20\x55\x48\x8b\xec",14,reinterpret_cast<void*>(&MappinPosition),reinterpret_cast<void**>(&originalMappinPosition)},
        {0x4E7E24,"\x48\x8b\xc4\x48\x89\x58\x10\x48\x89\x70\x18\x48\x89\x78\x20",15,reinterpret_cast<void*>(&Projection),reinterpret_cast<void**>(&originalProjection)},
        {0x2EE510,"\x48\x8b\xc4\x48\x89\x58\x10\x48\x89\x70\x18\x48\x89\x78\x20",15,reinterpret_cast<void*>(&Paint),reinterpret_cast<void**>(&originalPaint)},
        {0x2ECC64,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18",15,reinterpret_cast<void*>(&Allocate),reinterpret_cast<void**>(&originalAllocate)},
        {0x2F2708,"\x48\x8b\xc4\x48\x89\x58\x08\x4c\x89\x40\x18\x48\x89\x50\x10",15,reinterpret_cast<void*>(&Quad),reinterpret_cast<void**>(&originalQuad)},
        {0x2F2E40,"\x48\x8b\xc4\x48\x89\x58\x18\x48\x89\x48\x08\x55\x56\x57",14,reinterpret_cast<void*>(&Triangles),reinterpret_cast<void**>(&originalTriangles)},
        {0x2F3B10,"\x48\x8b\xc4\x48\x89\x58\x10\x55\x56\x57",10,reinterpret_cast<void*>(&Text),reinterpret_cast<void**>(&originalText)},
        {0x2F5ADC,"\x48\x8b\xc4\x48\x89\x58\x08\x4c\x89\x40\x18\x48\x89\x50\x10",15,reinterpret_cast<void*>(&Composite),reinterpret_cast<void**>(&originalComposite)},
    };
    for(const auto& h:hooks) if(std::memcmp(reinterpret_cast<void*>(base+h.rva),h.bytes,h.size)) {
        Log("[world-markers] signature mismatch at %llX; native placement retained\n",static_cast<unsigned long long>(h.rva));return false;
    }
    size_t made=0;
    for(const auto& h:hooks) {
        if(MH_CreateHook(reinterpret_cast<void*>(base+h.rva),h.detour,h.original)!=MH_OK) break;
        ++made;
    }
    bool ok=made==std::size(hooks);
    if(ok) for(const auto& h:hooks) if(MH_EnableHook(reinterpret_cast<void*>(base+h.rva))!=MH_OK) { ok=false;break; }
    if(ok) ok=InstallTextCall();
    if(!ok) {
        for(size_t i=0;i<made;++i) { MH_DisableHook(reinterpret_cast<void*>(base+hooks[i].rva));MH_RemoveHook(reinterpret_cast<void*>(base+hooks[i].rva)); }
        return false;
    }
    hooksReady.store(true,std::memory_order_release);
    Log("[world-markers] native projection ownership hooks installed\n");return true;
}
CVR_HOOK("WorldMarkers",cvr::hooks::Stage::Boot,83,Install);
}

void RegisterWidget(const RED4ext::Handle<RED4ext::ink::Widget>& widget,const RED4ext::Vector4& world) {
    const float point[3]={world.X,world.Y,world.Z};
    Remember(widget,point,world.W>.5f);
}

void RegisterProjection(const Handle<ink::Widget>& widget,const Handle<IScriptable>& projection) {
    if(!widget || !HudRoot(widget))return;
    std::lock_guard lock(projectionMutex);
    for(auto it=projectionOwners.begin();it!=projectionOwners.end();) {
        const auto live=it->second.widget.Lock();
        if(!live || (live.GetPtr()==widget.GetPtr() && it->first!=projection.GetPtr()))it=projectionOwners.erase(it);else ++it;
    }
    if(projection)projectionOwners[projection.GetPtr()]={WeakHandle<IScriptable>(projection),WeakHandle<ink::Widget>(widget),GetTickCount64()};
    else Remember(widget,nullptr,false);
}

// Called before the native geometry publication, while its writer still owns it.
extern "C" void CvrWorldMarkerTextTag(void* geometry) {
    const float encoded=EncodeDepth(textDepth);
    if(encoded && geometry && At<float>(geometry,0x188)==0 && At<float>(geometry,0x18C)==1) {
        At<float>(geometry,0x188)=encoded;
        CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerTexts.fetch_add(1,std::memory_order_relaxed));
    }
}
bool RewriteConstants(uint32_t size,const void* source,void* scratch) {
    if(!source || !scratch) return false;
    const int side=detail::t_view_side;
    if(IsInkCamera(source,size)) {
        const auto* p=static_cast<const float*>(source);
        if(side<0 || side>1) return false;
        inkCameras[side]={p[188],p[189],GetTickCount64()};
        CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerCameraUploads[side].fetch_add(1,std::memory_order_relaxed));
        if(offscreenPass) return false;
        const bool parallax=side==1 && Enabled();
        cvr::projection::ViewWindow window{};float squeeze=1;
        const bool shift=InkShift(side,p[188],p[189],window,squeeze);
        if(!parallax && !shift) return false;
        std::memcpy(scratch,source,size);
        auto* out=static_cast<float*>(scratch);
        // Clip x = 2x/w - 1 + z * out[2], clip y = 1 - 2y/h (+ out[7]); w = 1 for the flat camera.
        // The squeeze scales x about the canvas centre, then the centre moves to the view's shift --
        // in every copy of the canvas matrix (ShiftInkCamera). The parallax stays in the first: only
        // the replacement sprite/text shaders read it, and only they decode the depth tag.
        if(parallax) out[2]=EyeScale(p[188],p[189])*squeeze;
        if(shift) ShiftInkCamera(out,p,size,squeeze,window.shiftX,window.shiftY);
        return true;
    }
    // Direct uploads can decode here. Batched native composition bypasses these
    // uploaders; the replacement ink shaders also decode the same matrix tag.
    if(size!=112) return false;
    const auto* p=static_cast<const float*>(source);
    if(!TextMatrixDepth(p,size)) return false;
    std::memcpy(scratch,source,size);
    auto* out=static_cast<float*>(scratch);
    float shift=0;
    if(side>=0 && side<2) {
        CVR_DIAGNOSTIC(CyberpunkVR_WorldMarkerTextUploads[side].fetch_add(1,std::memory_order_relaxed));
        const auto& camera=inkCameras[side];
        if(side==1 && Enabled() && !offscreenPass && camera.width>0 && GetTickCount64()-camera.stamp<1000)
            shift=EyeScale(camera.width,camera.height)*camera.width*.5f;
    }
    ReprojectTextMatrix(out,shift);
    return true;
}
}

// Script-owned world indicators use the same paint/vertex path as mappins.
void VRWorldMarkerAnchor(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,void*,int64_t) {
    RED4ext::Handle<RED4ext::ink::Widget> widget;RED4ext::Vector4 world{};
    RED4ext::GetParameter(stack,&widget);RED4ext::GetParameter(stack,&world);stack->code++;
    cvr::markers::RegisterWidget(widget,world);
}

void VRWorldMarkerProjection(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,void*,int64_t) {
    RED4ext::Handle<RED4ext::ink::Widget> widget;RED4ext::Handle<RED4ext::IScriptable> projection;
    RED4ext::GetParameter(stack,&widget);RED4ext::GetParameter(stack,&projection);stack->code++;
    cvr::markers::RegisterProjection(widget,projection);
}
