#pragma once
#include <windows.h>
#include <string>

namespace cvr {
// A test switch: true when a file of this name sits next to the game executable. Read once by the
// caller (cache the result); never shipped, so no setting or overlay entry exists for it.
inline bool TestMarker(const char* name) {
    char path[MAX_PATH]{};
    if(!GetModuleFileNameA(nullptr,path,MAX_PATH))return false;
    if(char* slash=strrchr(path,'\\'))slash[1]=0;
    return GetFileAttributesA((std::string(path)+name).c_str())!=INVALID_FILE_ATTRIBUTES;
}
}
