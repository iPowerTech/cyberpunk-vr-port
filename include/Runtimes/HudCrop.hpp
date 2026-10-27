#pragma once
#include "Runtimes/HudLayout.hpp"

namespace cvr::hud {
// Native separate-window textures grow but do not shrink. A smaller requested
// crop is drawn at their top-left, while the HUD slot still uses the allocation's
// full size. Keep the content's centre in that actual texture, not just in the
// latest inkHudEntryInfo (which can be reset by a controller/animation).
inline Rect KeepCropCentered(Rect crop,float textureWidth,float textureHeight,float designScale) {
    if(!std::isfinite(designScale) || designScale<=0 || !std::isfinite(textureWidth) ||
        !std::isfinite(textureHeight) || textureWidth<=0 || textureHeight<=0 ||
        textureWidth>16384 || textureHeight>16384)return crop;
    const float cx=crop.x+crop.width*.5f,cy=crop.y+crop.height*.5f;
    // Stay below the next texel when the engine rounds the request. Otherwise
    // division/multiplication rounding could grow the texture on every update.
    crop.width=std::max(crop.width,std::nextafter(textureWidth/designScale,0.f));
    crop.height=std::max(crop.height,std::nextafter(textureHeight/designScale,0.f));
    crop.x=cx-crop.width*.5f;crop.y=cy-crop.height*.5f;
    return crop;
}
}
