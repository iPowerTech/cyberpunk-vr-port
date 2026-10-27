# Paused passenger window-combat pose

Captured from Cyberpunk 2077 PID18388 in the nomad prologue chase, with the pause
menu kept open. `PlayerPuppet.vehicleState` and the PSM blackboard both report
`gamePSMVehicle::Combat` (2), `Locomotion=Workspot` (9). Rig size: 620.

`vehicle_pose.bin` and `reference_full.bin` are 48-byte local Bone records derived
from the captured model-space FK and immutable reference arrays. Quaternion
normalization is applied during conversion; scale lanes are identity. Parents are
signed 16-bit indices. The raw snapshot is retained under
`build/passenger-window-paused-18388/` along with the original engine bone buffer.

The old pelvis basis requests 59.083 / 131.167 degrees at the clavicles; the left
side hits the existing 75-degree cap. Using the upper spine's basis for this
specific passenger pose requests about 50.30 degrees on both sides. This fixture
checks the isolated shoulder restriction and keeps the seated hips and legs byte-identical.

`low_camera_pose.bin` is the solved model-space FK captured from PID20928 on
2026-09-22, converted back to local transforms using the same parent table.
The engine's last bone scratch buffer is retained only as raw diagnostic data:
it can be reused after the animation callback and no longer reproduces the
plugin-owned solved FK on pause. This fixture uses the latter. Reference local
positions/rotations match this fixture set within 0.00000024/0.00000012.

In that capture the head was 65.436mm below Neck1. The actual HMD centre in
entity model space is (0.492536226, 0.114632202, 1.17155708); the old MAIN-eye
body target was 32.57mm off-centre. The new full Combat-body tests fit the
pelvis beneath HMD, preserve foot contacts and mount/camera controls, and
verify restoration of native lower-body transforms across cache replay.
