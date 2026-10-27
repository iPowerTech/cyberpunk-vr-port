#include "Runtimes/HudLayout.hpp"
#include <mutex>
#include <cstring>

namespace cvr::hud {
namespace { std::mutex mutex; LayoutSettings settings; LayoutStatus status; }
LayoutSettings GetLayoutSettings() { std::lock_guard lock(mutex);return settings; }
void SetLayoutSettings(const LayoutSettings& value) {
    std::lock_guard lock(mutex);
    for(size_t i=0;i<ElementCount;++i) settings[i]=Sanitize(value[i]);
}
LayoutStatus GetLayoutStatus() { std::lock_guard lock(mutex);return status; }
void SetLayoutStatus(const LayoutStatus& value) { std::lock_guard lock(mutex);status=value; }
bool ParseLayoutSetting(const char* line,LayoutSettings& value) {
    char key[80]{};
    for(size_t i=0;i<ElementCount;++i) {
        std::snprintf(key,sizeof(key),"hud_element_%s",Elements[i].key);
        const size_t n=std::strlen(key);
        if(std::strncmp(line,key,n)) continue;
        const char* p=line+n;while(*p==' ' || *p=='\t')++p;if(*p!='=')continue;
        ElementSettings s;
        if(std::sscanf(p+1,"%f , %f , %f , %f , %d",&s.x,&s.y,&s.scale,&s.opacity,&s.visible)==5)
            value[i]=Sanitize(s);
        return true;
    }
    return false;
}
void SaveLayoutSettings(FILE* file,const LayoutSettings& value) {
    for(size_t i=0;i<ElementCount;++i) {
        const auto s=Sanitize(value[i]);
        std::fprintf(file,"hud_element_%s=%.3f,%.3f,%.4f,%.4f,%d\n",Elements[i].key,s.x,s.y,s.scale,s.opacity,s.visible);
    }
}
}
