#pragma once
namespace cvr::stereo::probe {
inline thread_local bool internalCommands=false;
struct Scope {
    bool previous=internalCommands;
    Scope(){internalCommands=true;}
    ~Scope(){internalCommands=previous;}
};
}
