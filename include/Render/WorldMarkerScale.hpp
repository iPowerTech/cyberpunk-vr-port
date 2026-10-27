#pragma once
#include <cmath>

namespace cvr::markers {
// Native cached mappins place their texture at screenAnchor + cacheBounds.min.
// Scaling about this local point leaves the screen/world anchor fixed, even if
// an animation changes the cached bounds. The texture centre need not be it.
inline bool CachedMarkerPivot(float width,float height,float offsetX,float offsetY,float& x,float& y) {
    if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
       !std::isfinite(offsetX) || !std::isfinite(offsetY))return false;
    x=-offsetX/width;y=-offsetY/height;
    return std::abs(x)<=16 && std::abs(y)<=16;
}
struct MarkerScale {
    float appliedX=0,appliedY=0,nativeX=1,nativeY=1;
    bool applied=false;
    bool Target(float currentX,float currentY,float& x,float& y,float factor=.7f) const {
        if(!std::isfinite(currentX) || !std::isfinite(currentY) || !std::isfinite(factor) || !(factor>0))return false;
        if(applied && currentX==appliedX && currentY==appliedY)return false;
        x=currentX*factor;y=currentY*factor;
        return true;
    }
    // from: the game's scale the target was made from.
    void Commit(float x,float y,float fromX,float fromY){appliedX=x;appliedY=y;nativeX=fromX;nativeY=fromY;applied=true;}
    // The game's scale back, while the widget still carries ours (a newer game scale is the game's own).
    bool Restore(float currentX,float currentY,float& x,float& y) const {
        if(!applied || currentX!=appliedX || currentY!=appliedY)return false;
        x=nativeX;y=nativeY;
        return true;
    }
};
}
