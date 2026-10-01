// The node-scoped pose stamp. See include/Camera/PoseIdentity.hpp for what this is and which parts
// of upstream's version are deliberately not modelled.
//
// There is no logic here and there must not be: this runs inside the engine's node dispatcher,
// which is the hottest path in the renderer -- every node of every view, dozens of times a frame.
// It is a thread-local struct copy and nothing else. No lock, no atomic, no call into
// OpenXRManager. The pose is sampled ONCE per frame by the caller and handed in; sampling it per
// node would put a tracking query inside the render loop.

#include "Camera/PoseIdentity.hpp"

namespace cvr::camera {
namespace {
// Thread-local because the engine dispatches nodes on several job threads at once, and each one is
// inside its own view at its own moment.
thread_local PoseIdentity t_nodePose{};
}  // namespace

PoseIdentity CurrentNodePoseIdentity() { return t_nodePose; }

PoseIdentity SetNodePoseIdentity(const PoseIdentity& pose) {
    const PoseIdentity previous = t_nodePose;
    t_nodePose = pose;
    return previous;
}

}  // namespace cvr::camera
