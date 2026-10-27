#include "Hooks/PhotoModeCapture.hpp"
#include <stdexcept>
#include <iostream>

void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() try {
    using namespace cvr::capture;
    for (uint32_t type=0; type<8; ++type) {
        const bool photo = type == PhotoModeRequest;
        Check(OverrideSize(true,true,type,2560,2560)==photo,"non-photo capture gets VR dimensions");
        Check(!OverrideSize(true,false,type,2560,2560),"inactive request gets VR dimensions");
        Check(!OverrideSize(false,true,type,2560,2560),"failed query gets VR dimensions");
        Check(!OverrideSize(true,true,type,0,2560),"invalid VR size is applied");
        Check(PreserveReadbackSize(true,type,2560,2560,2560,2560)==photo,"non-photo readback skips clamp");
        Check(!PreserveReadbackSize(false,type,2560,2560,2560,2560),"world-widget hit test skips clamp");
        Check(!PreserveReadbackSize(true,type,456,256,2560,2560),"thumbnail dimensions replaced");
        Check(!PreserveReadbackSize(true,type,3840,2160,2560,2560),"unrelated resolution skips clamp");
    }
    // The same capture manager is reused across Photo Mode and save requests.
    // A prior photograph must not leave a latch that converts the next preview.
    for (uint32_t type : {3u,1u,3u,1u,3u})
        Check(OverrideSize(true,true,type,2560,2560)==(type==1),"capture type leaks across requests");
    std::cout << "PASS photo / save / inactive / world UI / request transitions\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
