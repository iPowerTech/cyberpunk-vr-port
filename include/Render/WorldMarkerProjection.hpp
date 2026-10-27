#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cvr::markers {
// Flat ink vertices normally have z=0. Carry a bounded inverse forward depth
// through the shared UI mesh; the replacement VS removes it before projection.
inline float EncodeDepth(float inverseDepth) {
    return std::isfinite(inverseDepth) && inverseDepth > 0 && inverseDepth <= 20
        ? -65536.0f - 4096.0f * inverseDepth : 0.0f;
}
inline float DecodeDepth(float value) {
    return value < -65536.0f && value >= -147456.0f
        ? (-value - 65536.0f) / 4096.0f : 0.0f;
}
inline float TextMatrixDepth(const float* p,size_t bytes) {
    if(!p || bytes!=112 || p[8]!=0 || p[9]!=0 || p[10]!=1 || p[15]!=1) return 0;
    return DecodeDepth(p[11]);
}
inline void ReprojectTextMatrix(float* p,float pixelsPerInverseDepth) {
    const float depth=TextMatrixDepth(p,112);
    if(!depth) return;
    p[11]=0;
    if(std::isfinite(pixelsPerInverseDepth)) p[3]+=depth*pixelsPerInverseDepth;
}
inline float ForwardDepth(const float point[3], const float eye[3], const float q[4]) {
    const double norm = double(q[0])*q[0]+double(q[1])*q[1]+double(q[2])*q[2]+double(q[3])*q[3];
    if (!std::isfinite(norm) || norm < .99 || norm > 1.01) return 0;
    // Game camera axes: X right, Y forward, Z up.
    const float forward[3] = {2*(q[0]*q[1]-q[2]*q[3]),
        1-2*(q[0]*q[0]+q[2]*q[2]), 2*(q[1]*q[2]+q[0]*q[3])};
    float depth=0;
    for (int i=0;i<3;++i) depth+=(point[i]-eye[i])*forward[i];
    return std::isfinite(depth) && depth >= .05f ? depth : 0;
}
inline float StereoScale(float ipd, float verticalFovDeg, float aspect, float zoom, bool mainRight) {
    if (!std::isfinite(ipd) || ipd <= 0 || ipd > .5f ||
        !std::isfinite(verticalFovDeg) || verticalFovDeg <= 1 || verticalFovDeg >= 170 ||
        !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(zoom) || zoom <= 0) return 0;
    const float scale = ipd * zoom / (std::tan(verticalFovDeg * .00872664626f) * aspect);
    return mainRight ? scale : -scale;
}
// The flat ink camera's matrix for a w x h canvas, exactly as the engine writes it.
inline bool IsInkMatrix(const float* m, float w, float h) {
    const float expected[16]={2/w,0,0,-1, 0,-2/h,0,1, 0,0,1,0, 0,0,0,1};
    for(int i=0;i<16;++i)
        if(!std::isfinite(m[i]) || std::abs(m[i]-expected[i])>1e-7f) return false;
    return true;
}
// Identify the game's flat ink camera, never a perspective/shadow/world camera.
inline bool IsInkCamera(const void* data, size_t bytes) {
    if (!data || bytes != 848) return false;
    const auto* p=static_cast<const float*>(data);
    const float w=p[188],h=p[189];
    if (!(w>=640 && w<=16384 && h>=360 && h<=16384)) return false;
    return IsInkMatrix(p,w,h);
}
// Squeezes clip x about the canvas centre and moves the centre to (shiftX, shiftY), in the camera's
// first matrix AND in every other copy of the canvas matrix the block carries. The engine writes that
// matrix five times (floats 0, 32, 48, 64 and 112; measured on the 2.31 HUD canvas). Ink's own sprite
// and text shaders read the first; the video widget's Bink material reads one of the others, so with
// only the first moved a tutorial popup's video stayed at the centred-frustum pixels in both eyes
// while its panel moved: doubled, and over the right eye's prompt. Returns the copies moved.
inline int ShiftInkCamera(float* out, const float* source, size_t bytes, float squeeze, float shiftX, float shiftY) {
    const float w=source[188],h=source[189];
    int copies=0;
    for(size_t m=0;m+16<=bytes/sizeof(float);m+=16) {
        if(m && !IsInkMatrix(source+m,w,h)) continue;
        out[m]*=squeeze;out[m+3]=out[m+3]*squeeze+shiftX;out[m+7]+=shiftY;
        if(m) ++copies;
    }
    return copies;
}
}
