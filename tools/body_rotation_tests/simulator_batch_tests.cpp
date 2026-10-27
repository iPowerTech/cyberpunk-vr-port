#include <windows.h>
#include <openxr/openxr.h>
#include "body_pose_batch.h"
#include <thread>
#include <atomic>
#include <stdexcept>
#include <iostream>
#include <fstream>
using namespace body_test_batch;
void Check(bool yes){if(!yes)throw std::runtime_error("incoherent simulator pose batch");}
Frame Make(float id){Frame f{};f.head.position.x=id;f.hands[0].position.x=id;f.hands[1].position.x=id;return f;}
int main()try{
    char directory[MAX_PATH]{},filename[MAX_PATH]{};
    Check(GetTempPathA(MAX_PATH,directory)!=0);Check(GetTempFileNameA(directory,"CVR",0,filename)!=0);
    {std::ofstream file(filename);file<<R"({"enabled":true,"x":0.003,"y":1.7,"z":-0.002,"yaw":0.031,"pitch":-0.2,"roll":0.15,"lx":-0.24,"ly":-0.3,"lz":-0.4,"lyaw":0.12,"lpitch":-0.15,"rx":0.24,"ry":-0.3,"rz":-0.4,"ryaw":-0.12,"rpitch":-0.3})";}
    const auto cmd=Read(filename);DeleteFileA(filename);
    Check(cmd.valid&&cmd.enabled&&std::abs(cmd.head[0]-.003f)<1e-6f&&std::abs(cmd.head[1]-1.7f)<1e-6f);
    Check(std::abs(cmd.hands[0][0]+.24f)<1e-6f&&std::abs(cmd.hands[1][4]+.3f)<1e-6f);
    Frame f{};Check(!At(1,f));Publish(true,Make(1));Check(At(1,f)&&f.head.position.x==1);
    Publish(true,Make(2));Check(At(1,f)&&f.hands[1].position.x==1);Check(At(2,f)&&f.head.position.x==2);
    Check(!At(0,f));Publish(false,{});Check(!At(1,f));Publish(true,Make(3));Check(At(1,f)&&f.head.position.x==3);
    std::atomic<bool> failed=false;
    std::thread writer([]{for(int i=4;i<100004;++i)Publish(true,Make(float(i)));});
    for(XrTime i=3;i<100003;++i){Check(At(i,f));
        if(f.head.position.x!=f.hands[0].position.x || f.head.position.x!=f.hands[1].position.x)failed=true;
        const auto first=f;Check(At(i,f));Check(first.head.position.x==f.head.position.x);
    }
    writer.join();Check(!failed);
    std::cout<<"PASS fractional IPC coordinates, atomic three-device publish, same-XrTime reuse, disable/reset and 100000 concurrent queries\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
