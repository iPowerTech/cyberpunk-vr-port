#pragma once
// Test-only simulator transport. Each command publishes all devices together;
// every locate at an identical XrTime gets an identical immutable snapshot.
#include <array>
#include <mutex>
#include <cmath>
#include "json.h"
namespace body_test_batch {
struct Frame { XrPosef head{},hands[2]{}; };
struct Command { bool valid=false,enabled=false;float head[6]{},hands[2][5]{}; };
inline std::mutex mutex;
inline bool enabled=false;
inline Frame current{};
struct Cached {XrTime time=0;Frame frame{};};
inline std::array<Cached,128> cache{};
inline unsigned cursor=0;
inline void Publish(bool on,const Frame& f){
    std::lock_guard lock(mutex);current=f;enabled=on;
    if(!on){cache={};cursor=0;}
}
inline bool At(XrTime time,Frame& f){
    std::lock_guard lock(mutex);if(!enabled || time<=0)return false;
    for(const auto& c:cache)if(c.time==time){f=c.frame;return true;}
    cache[cursor++%cache.size()]={time,current};f=current;return true;
}
inline Command Read(const std::string& path){
    Command cmd;FILE* file=nullptr;
    if(fopen_s(&file,path.c_str(),"rb")!=0 || !file)return cmd;
    char buffer[2048]{};const auto n=fread(buffer,1,sizeof(buffer)-1,file);fclose(file);
    if(n==sizeof(buffer)-1)return cmd;
    json::Object obj(buffer);if(!obj.valid() || !obj.has("enabled"))return cmd;
    cmd.enabled=obj.boolean("enabled",false);
    if(!cmd.enabled){cmd.valid=true;return cmd;}
    const char* head[]{"x","y","z","yaw","pitch","roll"};
    const char* hands[2][5]{{"lx","ly","lz","lyaw","lpitch"},{"rx","ry","rz","ryaw","rpitch"}};
    for(int i=0;i<6;++i){if(!obj.has(head[i]))return cmd;cmd.head[i]=float(obj.number(head[i],0.0));if(!std::isfinite(cmd.head[i]))return cmd;}
    for(int h=0;h<2;++h)for(int i=0;i<5;++i){if(!obj.has(hands[h][i]))return cmd;cmd.hands[h][i]=float(obj.number(hands[h][i],0.0));if(!std::isfinite(cmd.hands[h][i]))return cmd;}
    cmd.valid=true;return cmd;
}
}
