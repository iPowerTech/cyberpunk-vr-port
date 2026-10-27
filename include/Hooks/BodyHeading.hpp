#pragma once

// Independent tracking-to-world reference, with native heading as the fallback
// outside on-foot VRIK. A stopped follower does not discard this reference.
float BodyTrackingYaw(float nativeFallback);
bool ReadBodyTrackingYaw(float* yaw);
float BodyHeadingOffset();
float BodyCentreYaw(float nativeFallback);
void BodyHeadingReset();
bool BodyHeadingReady();
