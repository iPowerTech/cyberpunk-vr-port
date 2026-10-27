#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::input {
void PublishScannerVision(int state);
int ScannerVision(); // -1 means no recent game-state sample

class ScannerRequest {
    uint64_t started{};
    bool pending{},accepted{};
public:
    void Update(bool& requested,int vision,uint64_t now) {
        if(!requested){pending=accepted=false;started=0;return;}
        if(!pending){pending=true;started=now;accepted=false;}
        if(vision==1)accepted=true;
        // An input request is not proof that the game opened the scanner.
        // Release rejected/stale requests and respect a game-driven exit.
        if((accepted && vision==0) || (vision!=1 && now>=started && now-started>=750)) {
            requested=false;pending=accepted=false;started=0;
        }
    }
};
// The runtime reports the controller's grip centre, below/outside the ear,
// rather than the point where the fingers touch the headset.
struct ScannerEarZone {
    float x=-0.18f, y=-0.08f, z=0.08f;
    float radius=0.22f, margin=0.04f;
    bool Contains(float px,float py,float pz,bool armed) const {
        if(!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) || px>-.04f)return false;
        const float r0=radius>.02f && radius<=.60f ? radius : .22f;
        const float extra=margin>=0 && margin<=.30f ? margin : .04f;
        const float r=r0+(armed?extra:0);
        const float dx=px-x,dy=py-y,dz=pz-z;
        return dx*dx+dy*dy+dz*dz<=r*r;
    }
};
}
