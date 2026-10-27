#pragma once
#include "Runtimes/OpenXRManager.hpp"
#include "Camera/PoseAddressLedger.hpp"
#include <cstdint>
extern "C" {
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_PoseIdFinal[2];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_PoseIdFinalMiss[2];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_PoseIdVrikSource;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_PoseIdVrikInputChanged;
}

namespace cvr::camera {
struct PoseIdentity {
    uint64_t poseId{},writeId{};
    uintptr_t component{};
    uint32_t view{};
    OpenXRHeadPose head{};
    XrPosef localHead{};
    // Rendering stamp, populated only by the native node dispatcher. It is not
    // a pose-sample id: several rendered frames can use the same tracked pose.
    uint32_t renderFrameId{};
    bool hasRenderFrameId{};
    bool externalView{}; // selected MAIN is a detached camera, not the player's placed FPP
    explicit operator bool() const { return poseId && writeId && head.valid; }
};
using PoseReceipt=PoseAddressLedger<PoseIdentity>::Receipt;

uint64_t NextPoseIdentity();
void InvalidatePoseAddress(uintptr_t address);
PoseReceipt CapturePoseAddress(uintptr_t address);
bool TransferPoseAddress(const PoseReceipt& source,uintptr_t destination);
void PublishComponentPose(uintptr_t component,uint32_t view,uint64_t poseId,
                          const OpenXRHeadPose& head);
void PublishCameraSetupPose(uintptr_t address,uintptr_t component,uint32_t view,uint64_t poseId,
                            const OpenXRHeadPose& head,bool externalView=false);
void PublishSerializedPose(uintptr_t destination,const PoseReceipt& source);
// Exact component whose native SerializeSetup call contains LocateCamera.
uintptr_t CurrentSerializedCameraComponent();
struct WeightedPoseReceipt { PoseReceipt source;float weight{}; };
bool PublishBlendedPose(uintptr_t destination,std::span<const WeightedPoseReceipt> inputs);
bool ReadCameraPoseIdentity(uintptr_t camera,uint32_t view,PoseIdentity* out);
bool ReadContextPoseIdentity(uintptr_t context,PoseIdentity* out);

// Populated at node entry, restored at node exit. Copies after node exit pass
// their saved value explicitly instead of reading an unrelated parent scope.
PoseIdentity CurrentNodePoseIdentity();
PoseIdentity SetNodePoseIdentity(const PoseIdentity& pose);
}
