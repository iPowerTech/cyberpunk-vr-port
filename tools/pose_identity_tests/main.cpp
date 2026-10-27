#include "Camera/PoseAddressLedger.hpp"
#include "Camera/ImagePoseLedger.hpp"
#include "Camera/ImagePoseMath.hpp"
#include "Runtimes/CaptureBufferLeases.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
struct Token { uint64_t id{},origin{1};uint32_t view{1}; };
using Cameras=cvr::camera::PoseAddressLedger<Token,4>;
using Images=cvr::camera::ImagePoseLedger<Token>;
const cvr::camera::PoseFingerprint fp{{46297873,-322030768,23984874},{0,0,0,0x3f800000}};
void Check(bool ok,const char* why) { if(!ok) { std::cerr<<why<<'\n';std::exit(1); } }
void Near(float a,float b,const char* why) { Check(std::abs(a-b)<2e-6f,why); }
void IdenticalPose() {
    Cameras cameras;
    cameras.Publish(10,fp,{1,1,1});const auto first=cameras.Capture(10,fp);
    Check(cameras.Transfer(first,fp,20,fp),"first serialization was lost");
    cameras.Publish(10,fp,{2,1,1});const auto next=cameras.Capture(10,fp);
    Check(cameras.Transfer(next,fp,30,fp),"second serialization was lost");
    Check(cameras.Capture(20,fp).payload.id==1,"identical coordinates replaced an old image's identity");
    Check(cameras.Capture(30,fp).payload.id==2,"identical coordinates collapsed two generations");
    cameras.Publish(40,fp,{(uint64_t{1}<<63)|42,1,2});
    Check(cameras.Capture(40,fp).payload.id==((uint64_t{1}<<63)|42),"native sample identity lost its high bits");
}
void OwnerGeneration() {
    Cameras cameras;cameras.Publish(10,fp,{1});const auto old=cameras.Capture(10,fp);
    cameras.Invalidate(10);cameras.Publish(10,fp,{2});
    Check(!cameras.Transfer(old,fp,20,fp),"reused source address accepted an obsolete receipt");
    Check(!cameras.Capture(20,fp),"reused address exposed the wrong source identity");
}
void ChangedDuringCopy() {
    Cameras cameras;cameras.Publish(10,fp,{1});const auto old=cameras.Capture(10,fp);
    cameras.Publish(10,fp,{2});
    Check(!cameras.Transfer(old,fp,20,fp),"same coordinates hid a source publication during copy");
    auto moved=fp;++moved.position[0];
    const auto current=cameras.Capture(10,fp);
    Check(!cameras.Transfer(current,moved,20,moved),"unpublished source mutation was accepted");
}
void UntrackedCopy() {
    Cameras cameras;cameras.Publish(20,fp,{7});
    Check(!cameras.Transfer({},fp,20,fp),"untracked copy invented a source ID");
    Check(!cameras.Capture(20,fp),"untracked copy retained the previous occupant's ID");
}
void BoundedLedger() {
    Cameras cameras;
    for(uintptr_t i=1;i<30;++i)cameras.Publish(i,fp,{i});
    Check(!cameras.Capture(1,fp),"evicted object remained addressable");
    Check(cameras.Capture(29,fp).payload.id==29,"latest object was evicted");
    for(int i=0;i<8;++i)cameras.Publish(29,fp,{uint64_t(40+i)});
    Check(cameras.Capture(29,fp).payload.id==47,"old ring slot erased a fresh publication at the same address");
}
void CachedCameraLifetime() {
    Cameras cameras;
    cameras.Publish(20,fp,{7,1,2});
    for(uint64_t id=10;id<10000;++id)cameras.Publish(10,fp,{id,1,1});
    const auto held=cameras.Capture(20,fp);
    Check(held && held.payload.id==7,"unrelated MAIN publications expired a cached VRCAM pose");
    Check(cameras.Transfer(held,fp,30,fp),"cached VRCAM descriptor could not be copied again");
    cameras.Publish(40,fp,{8});
    for(uintptr_t address=50;address<100;++address) {
        Check(bool(cameras.Capture(20,fp)),"active cached camera was evicted before unused addresses");
        cameras.Publish(address,fp,{address});
    }
    Check(cameras.Capture(20,fp).payload.id==7,"cached camera lifetime depends on other addresses");
}
void Submission() {
    Images images;images.Record(10,100,{8},false);
    Check(!images.Read(100),"recording published pixels before queue submission");
    images.Submit(3,10,1000);
    Check(images.Read(100).pose.id==8 && images.Read(100).queue==3,"submitted image lost its source");
}
void ResetBeforeSubmit() {
    Images images;images.Record(10,100,{8},false);images.Reset(10);images.Submit(3,10,1000);
    Check(!images.Read(100),"discarded command list published a pose");
}
void ImageReuse() {
    Images images;images.Record(10,100,{11,1,1},false);images.Record(11,200,{12,1,2},true);
    images.Submit(3,10,1);images.Submit(3,11,1);const auto retained=images.LatestStable();
    images.Reset(10);images.Record(10,100,{13,1,1},false);images.Submit(3,10,2);
    Check(images.Read(100).pose.id==13,"MAIN did not advance");
    Check(images.LatestStable().pose.id==12 && retained.pose.id==12,"reused VRCAM image inherited MAIN's new pose");
    images.Reset(11);images.Record(11,200,{},true);images.Submit(3,11,3);
    Check(images.Read(200).pose.id==0,"unlabelled pixels inherited a previous image's pose");
}
void LatestWrite() {
    Images images;images.Record(10,100,{1},true);images.Record(10,200,{2},true);images.Record(10,100,{3},true);
    images.Submit(4,10,1);
    Check(images.LatestStable().resource==100 && images.LatestStable().pose.id==3,"target deduplication reordered the final copy");
}
void ObjectLifetime() {
    Images images;images.Record(10,100,{1},true);images.Submit(4,10,1);
    images.ForgetResource(100);images.ForgetList(10);
    Check(!images.Read(100) && !images.LatestStable(),"destroyed resource retained an image identity");
    images.Submit(4,10,2);Check(!images.Read(100),"destroyed command-list address replayed its previous recording");
    images.Record(11,100,{2},true);images.Submit(4,11,3);images.ForgetResource(100);
    images.Record(12,100,{3},true);images.Submit(4,12,4);images.Submit(4,11,5);
    Check(images.Read(100).pose.id==3,"old recording labelled a new resource at a reused address");
}
void Reservations() {
    cvr::capture::BufferLeases leases;
    Check(leases.Pin(10),"ready image cannot be pinned");
    Check(!leases.BeginWrite(10),"producer can replace pixels selected by the consumer");
    Check(leases.BeginWrite(20),"unused buffer requires an artificial frame wait");
    Check(!leases.Pin(20),"consumer selected a buffer with unpublished pixels");
    leases.EndWrite(20);Check(leases.Pin(20),"completed publication stayed inaccessible");
    leases.Unpin(10);Check(leases.BeginWrite(10),"reader reservation leaked");
    leases.EndWrite(10);leases.Unpin(20);Check(leases.BeginWrite(20),"reused VRCAM slot stayed pinned");
}
void EyeRebase() {
    const XrPosef old{{0,0,0,1},{1,2,3}};
    const XrPosef current{{0,std::sin(.4f),0,std::cos(.4f)},{-.3f,1.7f,.2f}};
    for(float eye:{-.0325f,.0325f}) {
        const XrPosef prior{{0,0,0,1},{old.position.x+eye,old.position.y,old.position.z}};
        const auto result=cvr::camera::RebaseImageEyePose(old,prior,current);
        const auto expected=RotateVector(current.orientation,{eye,0,0});
        Near(result.position.x,current.position.x+expected.x,"eye offset mixed two head samples");
        Near(result.position.y,current.position.y+expected.y,"image label lost its own height");
        Near(result.position.z,current.position.z+expected.z,"image label lost its own translation");
        Near(result.orientation.y,current.orientation.y,"image label lost its own rotation");
    }
}
bool SameSample(const Token& a,const Token& b) {
    return a.id==b.id && a.origin==b.origin && a.view==1 && b.view==1;
}
void CameraBlend() {
    Cameras cameras;
    cameras.Publish(10,fp,{81});cameras.Publish(11,fp,{81});
    Cameras::BlendInput inputs[]={{cameras.Capture(10,fp),fp,.3f},{cameras.Capture(11,fp),fp,.7f}};
    auto blended=fp;
    ++blended.rotation[2];++blended.position[0]; // native nlerp/fixed-point conversion
    Check(cameras.TransferBlend(inputs,20,blended,SameSample),"native blend lost a common HMD sample");
    Check(cameras.Capture(20,blended).payload.id==81,"blend replaced the input's identity");
    Images images;images.Record(1,100,cameras.Capture(20,blended).payload,false);images.Submit(2,1,1);
    Check(images.Read(100).pose.id==81,"blended MAIN image lost its exact sample");
}
void CameraBlendReject() {
    Cameras cameras;cameras.Publish(10,fp,{81});cameras.Publish(11,fp,{82});
    Cameras::BlendInput inputs[]={{cameras.Capture(10,fp),fp,.5f},{cameras.Capture(11,fp),fp,.5f}};
    cameras.Publish(20,fp,{80});
    Check(!cameras.TransferBlend(inputs,20,fp,SameSample),"different head samples were labelled as one");
    Check(!cameras.Capture(20,fp),"failed blend retained an old image label");
    inputs[1].source={};inputs[0].weight=.999999f;inputs[1].weight=.000001f;
    Check(!cameras.TransferBlend(inputs,20,fp,SameSample),"small native camera weight was silently discarded");
    inputs[0].weight=1;inputs[1].weight=0;
    Check(cameras.TransferBlend(inputs,20,fp,SameSample),"zero-weight camera revoked a valid source");
    inputs[0].weight=.5f;
    Check(!cameras.TransferBlend(inputs,20,fp,SameSample),"scaled head translation kept an unscaled label");
}
void CameraBlendStale() {
    Cameras cameras;cameras.Publish(10,fp,{81});cameras.Publish(11,fp,{81});
    Cameras::BlendInput inputs[]={{cameras.Capture(10,fp),fp,.5f},{cameras.Capture(11,fp),fp,.5f}};
    cameras.Publish(11,fp,{81});
    Check(!cameras.TransferBlend(inputs,20,fp,SameSample),"identical coordinates hid a replaced blend input");
    inputs[1].source=cameras.Capture(11,fp);++inputs[1].current.rotation[0];
    Check(!cameras.TransferBlend(inputs,20,fp,SameSample),"changed input inherited an old pose label");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;const std::string name=argv[1];
    if(name=="camera_blend") { CameraBlend();return 0; }
    if(name=="camera_blend_reject") { CameraBlendReject();return 0; }
    if(name=="camera_blend_stale") { CameraBlendStale();return 0; }
    if(name=="identical_pose")IdenticalPose();else if(name=="owner_generation")OwnerGeneration();
    else if(name=="changed_during_copy")ChangedDuringCopy();else if(name=="untracked_copy")UntrackedCopy();
    else if(name=="bounded_ledger")BoundedLedger();else if(name=="submission")Submission();
    else if(name=="cached_camera_lifetime")CachedCameraLifetime();
    else if(name=="reset_before_submit")ResetBeforeSubmit();else if(name=="image_reuse")ImageReuse();
    else if(name=="latest_write")LatestWrite();else if(name=="object_lifetime")ObjectLifetime();
    else if(name=="reservations")Reservations();else if(name=="eye_rebase")EyeRebase();else return 2;
}
