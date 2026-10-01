// PoseIdentity -- which head pose a rendered node belongs to.
//
// Upstream 0.1.7's native frame generator (src/Framegen) needs to recognise the SAME frame across
// three stages that do not see each other: RecordTags collects the game's colour/motion/depth as
// Streamline tags them, Evaluate fires when the upscaler runs, and the presenter picks the pair up
// later. Matching them by arrival order is wrong the moment a frame is dropped or the two eyes
// interleave, so each stage stamps the frame with the identity of the pose it was rendered from and
// the later stages refuse anything whose stamp has moved.
//
// THIS IS A REDUCED PORT OF UPSTREAM'S HEADER, DELIBERATELY.
//
// Upstream backs the same struct with a camera pose-address ledger -- PoseAddressLedger plus
// CapturePoseAddress / TransferPoseAddress / PublishBlendedPose / ReadCameraPoseIdentity and a
// dozen more -- fed from twenty-four files including LocateCamera, PatchCamera, FinalCamera,
// CameraLink, AnimPose and WeaponAim. This fork keeps its own camera logic in all of those (it was
// measured to work better here), so taking that ledger would mean abandoning it.
//
// src/Framegen/Inputs.cpp turns out to use exactly ONE function and FIVE fields, so that is what
// this provides, fed from this fork's own pose path. The name, namespace and signature are
// upstream's on purpose: if the full ledger is ever adopted, Inputs.cpp does not change.
//
// WHAT IS NOT MODELLED, and why it is safe:
//   writeId      upstream's camera-write identity, produced by the ledger. Set equal to poseId, so
//                operator bool() degenerates to "there is a pose sample and it is valid" -- the
//                honest version of the question this fork can answer.
//   component    the owning camera component address. Never read by Inputs.cpp.
//   externalView true when MAIN is a detached camera. Never read by Inputs.cpp.
#pragma once

#include "Runtimes/OpenXRManager.hpp"
#include <cstdint>

namespace cvr::camera {

struct PoseIdentity {
    uint64_t poseId{}, writeId{};
    uintptr_t component{};
    uint32_t view{};              // 1 = MAIN, 2 = VRCAM. 0 = no view, which bool() rejects.
    OpenXRHeadPose head{};
    // Rendering stamp, populated only by the native node dispatcher. It is not a pose-sample id:
    // several rendered frames can use the same tracked pose.
    uint32_t renderFrameId{};
    bool hasRenderFrameId{};
    bool externalView{};
    explicit operator bool() const { return poseId && writeId && head.valid; }
};

// Published at node entry and restored at node exit by Stereo/NodeDispatch.cpp, which already
// saves and restores the executing view the same way -- SceneDrv re-enters the dispatcher per
// pass, so a nested node must not leave the parent's stamp behind.
PoseIdentity CurrentNodePoseIdentity();
PoseIdentity SetNodePoseIdentity(const PoseIdentity& pose);   // returns the previous value

}  // namespace cvr::camera
