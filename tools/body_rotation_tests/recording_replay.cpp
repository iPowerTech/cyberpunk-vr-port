#include "Runtimes/TrackedBodyYaw.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstdint>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    cvr::body::TrackedBodyYaw tracker;
    cvr::body::TrackingFrame frame;
    static_assert(sizeof(frame)==112);
    uint64_t now{};float old{},expected{};uint8_t flags{};
    double oldSum=0,newSum=0;float oldMax=0,newMax=0;uint64_t count=0;
    while(input.read(reinterpret_cast<char*>(&frame),sizeof(frame))) {
        if(!input.read(reinterpret_cast<char*>(&now),sizeof(now)) || !input.read(reinterpret_cast<char*>(&old),4) ||
           !input.read(reinterpret_cast<char*>(&expected),4) || !input.read(reinterpret_cast<char*>(&flags),1))return 3;
        if(flags&1)tracker.Reset();
        const auto update=tracker.Update(frame);
        const auto predicted=tracker.Predict(now);
        if((flags&2) && update.valid && predicted.valid) {
            const auto error=[&](float v){return std::abs(std::remainder(v-expected,6.28318530718f))*57.2957795f;};
            const float before=error(old),after=error(predicted.yaw);
            oldSum+=before;newSum+=after;oldMax=std::max(oldMax,before);newMax=std::max(newMax,after);++count;
        }
    }
    if(!count)return 4;
    std::cout<<"{\"samples\":"<<count<<",\"recorded_mean_deg\":"<<oldSum/count<<",\"replay_mean_deg\":"<<newSum/count
             <<",\"recorded_max_deg\":"<<oldMax<<",\"replay_max_deg\":"<<newMax<<"}\n";
}
