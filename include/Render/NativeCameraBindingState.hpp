#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cvr::stereo::camera_upload {
using ReadState=bool(*)(uintptr_t,void*,size_t);
struct Context {
    uint64_t context{},state{},list{};
    bool operator==(const Context&)const=default;
    explicit operator bool()const{return context && state && list;}
};
struct CpuSlot {
    uint64_t layout{},table{},address{},descriptor{};
    uint32_t parameter{},index{};
    bool operator==(const CpuSlot&)const=default;
};
inline bool ResolveCameraSlotOnce(ReadState read,uint64_t state,uint32_t stage,CpuSlot& out){
    out={};
    if(!read || !state || state>UINT64_MAX-0x70 || stage>1 ||
        !read(state+0x68,&out.layout,8) || !read(state+0x28,&out.table,8) || !out.layout || !out.table ||
        out.layout>UINT64_MAX-0x473)return false;
    uint8_t parameter{};
    if(!read(out.layout+0x455+uint64_t(stage)*0x1c,&parameter,1) || parameter>=64)return false;
    const auto entry=out.layout+0x38+uint64_t(parameter)*16;
    uint32_t offset{};uint16_t first{};
    if(!read(entry+4,&offset,4) || !read(entry+8,&first,2) || first>1 || offset>8190)return false;
    out.parameter=parameter;out.index=offset+1-first;
    if(out.table>UINT64_MAX-uint64_t(out.index)*8-8)return false;
    out.address=out.table+uint64_t(out.index)*8;
    return read(out.address,&out.descriptor,8) && out.descriptor;
}
// Layout is guarded by the complete native 0x1F3978 code fingerprint. Re-read
// the native shadow so a changed table/layout cannot supply a mixed snapshot.
inline bool ResolveCameraSlot(ReadState read,uint64_t state,uint32_t stage,CpuSlot& out){
    CpuSlot a,b;
    if(!ResolveCameraSlotOnce(read,state,stage,a) || !ResolveCameraSlotOnce(read,state,stage,b) || a!=b)return false;
    out=a;return true;
}
struct Binding {
    uint32_t handle{};uint64_t serial{},descriptor{};
};
struct BindingScope {
    Context context;
    std::array<Binding,2> original;
    uint64_t replacement{};
    uint32_t requested{},applied{},restored{},preserved{};
};
using RestoreBinding=bool(*)(void*,Context,uint32_t,uint32_t);
inline bool RestoreScope(ReadState read,Context current,BindingScope& scope,RestoreBinding bind,void* user){
    if(!scope.applied)return true;
    if(!current || current!=scope.context || !bind)return false;
    bool complete=true;
    for(uint32_t stage=0;stage<2;++stage){const uint32_t bit=1u<<stage;if(!(scope.applied&bit))continue;
        CpuSlot slot;
        if(ResolveCameraSlot(read,current.state,stage,slot) && slot.descriptor!=scope.replacement){
            scope.preserved|=bit;scope.applied&=~bit;continue;
        }
        if(!bind(user,current,scope.original[stage].handle,bit) ||
            !ResolveCameraSlot(read,current.state,stage,slot) || slot.descriptor!=scope.original[stage].descriptor){complete=false;continue;}
        scope.restored|=bit;scope.applied&=~bit;
    }
    return complete;
}
class BindingTracker {
    Context context_;
    std::array<Binding,2> bindings_{};
    uint64_t serial_{};
public:
    void Clear(){context_={};bindings_={};}
    void Observe(ReadState read,Context context,uint32_t handle,uint32_t stages){
        if(!(stages&3))return;
        if(!context){Clear();return;}
        if(context_!=context){Clear();context_=context;}
        for(uint32_t stage=0;stage<2;++stage)if(stages&(1u<<stage)){
            CpuSlot slot;auto& binding=bindings_[stage];binding={};binding.serial=++serial_;
            if(handle && ResolveCameraSlot(read,context.state,stage,slot)){binding.handle=handle;binding.descriptor=slot.descriptor;}
        }
    }
    bool Capture(ReadState read,Context context,uint32_t stages,BindingScope& scope)const{
        scope={};
        if(!context || context!=context_ || !stages || stages>3)return false;
        for(uint32_t stage=0;stage<2;++stage)if(stages&(1u<<stage)){
            CpuSlot slot;const auto& saved=bindings_[stage];
            if(!saved.handle || !saved.serial || !ResolveCameraSlot(read,context.state,stage,slot) || slot.descriptor!=saved.descriptor)return false;
        }
        scope.context=context;scope.original=bindings_;scope.requested=stages;return true;
    }
    bool Changed(const BindingScope& scope,uint32_t stage)const{
        return stage>1 || context_!=scope.context || bindings_[stage].serial!=scope.original[stage].serial;
    }
};
}
