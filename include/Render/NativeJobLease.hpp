#pragma once
#include <array>

namespace cvr::stereo {
struct JobLeaseApi {
    void(*retain)(void*);
    void(*release)(void**);
    bool(*complete)(void*);
};
// Retain while the source native job scope is live, then seal after all work was
// queued. Hold both begin/end groups: Advance moves the pending end group to
// begin and creates a new empty end, which alone would falsely appear complete.
class NativeJobLease {
public:
    explicit NativeJobLease(JobLeaseApi api):api_(api){}
    ~NativeJobLease(){for(auto& group:groups_)if(group)api_.release(&group);}
    NativeJobLease(const NativeJobLease&)=delete;
    NativeJobLease& operator=(const NativeJobLease&)=delete;
    bool Capture(void* begin,void* end) {
        if(sealed_ || !begin || !end || !api_.retain || !api_.release || !api_.complete)return false;
        groups_={begin,begin==end?nullptr:end};for(auto* group:groups_)if(group)api_.retain(group);sealed_=true;return true;
    }
    bool Complete()const {
        if(!sealed_)return false;for(auto* group:groups_)if(group && !api_.complete(group))return false;return true;
    }
private:
    JobLeaseApi api_;std::array<void*,2> groups_{};bool sealed_{};
};
}
