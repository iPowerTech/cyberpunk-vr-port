#include "Render/NativeJitter.hpp"
#include <cstdio>
int main() {
    unsigned counter,width,height;
    while(std::scanf("%u %u %u",&counter,&width,&height)==3) {
        const auto result=cvr::stereo::PeekR2Jitter(counter,width,height);
        std::printf("%.9g %.9g %.9g %.9g %u\n",result.pixelX,result.pixelY,result.clipX,result.clipY,result.phase);
    }
}
