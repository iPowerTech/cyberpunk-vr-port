#include "Core/LiveControls.hpp"
#include <cstddef>
#include <iostream>
int main() {
    std::cout<<"{\"prediction\":"<<offsetof(LiveControls,xrWheelPrediction)
             <<",\"horizon\":"<<offsetof(LiveControls,xrWheelPredictionMs)
             <<",\"limit\":"<<offsetof(LiveControls,xrWheelSteerMaxDeg)
             <<",\"dead\":"<<offsetof(LiveControls,xrWheelSteerDeadDeg)<<"}\n";
}
