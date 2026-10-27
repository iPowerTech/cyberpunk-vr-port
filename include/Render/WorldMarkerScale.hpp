#pragma once
#include <cmath>

namespace cvr::markers {
struct MarkerScale {
    float appliedX=0,appliedY=0;
    bool applied=false;
    bool Target(float currentX,float currentY,float& x,float& y) const {
        if(!std::isfinite(currentX) || !std::isfinite(currentY))return false;
        if(applied && currentX==appliedX && currentY==appliedY)return false;
        x=currentX*.7f;y=currentY*.7f;
        return true;
    }
    void Commit(float x,float y){appliedX=x;appliedY=y;applied=true;}
};
}
