#pragma once
#include "Render/NativeGraphJobs.hpp"
#include <limits>

namespace cvr::stereo::graph_jobs {
inline constexpr uint32_t ExecutionCallbackReturn=0x14B1E0;
inline constexpr uint32_t AggregateExecutionCallbackReturn=0x14324C;
enum class TaskCompletionFailure:uint32_t {
    None,Arguments,Caller,AlreadyCaptured,MissingApi,Read,HandleAddress,Group,
    PrerequisiteAlias,References,NoCallbackToken,Changed
};

// Only the audited AC4A04 scheduler callback may supply this worker context.
// The caller proves the payload/graph and native body fingerprints separately.
// Native reference callbacks must not throw; the caller serializes all methods.
class TaskCompletionLease {
public:
    TaskCompletionLease()=default;
    ~TaskCompletionLease(){if(group_)api_.release(&group_);}
    TaskCompletionLease(const TaskCompletionLease&)=delete;
    TaskCompletionLease& operator=(const TaskCompletionLease&)=delete;
    TaskCompletionLease(TaskCompletionLease&&)=delete;
    TaskCompletionLease& operator=(TaskCompletionLease&&)=delete;

    bool CaptureBeforeCallback(uint64_t worker,uint64_t caller,uint64_t imageBase,uint64_t rootPrerequisite,
                               ReadMemory read,JobLeaseApi api)try{
        if(captured_){failure_=TaskCompletionFailure::AlreadyCaptured;return false;}
        failure_=TaskCompletionFailure::Arguments;
        if(!read || !Pointer(rootPrerequisite,0x20) || !imageBase ||
            imageBase>UINT64_MAX-ExecutionCallbackReturn)return false;
        failure_=TaskCompletionFailure::Caller;
        // Both guarded scheduler bodies retain this local handle and decrement
        // the original queued completion token only after the callback returns.
        const uint64_t handleOffset=caller==imageBase+ExecutionCallbackReturn?0x68:
            caller==imageBase+AggregateExecutionCallbackReturn?0x150:0;
        if(!handleOffset)return false;
        failure_=TaskCompletionFailure::Arguments;if(!Pointer(worker,handleOffset+8))return false;
        failure_=TaskCompletionFailure::MissingApi;if(!api.retain || !api.release || !api.complete)return false;
        Identity first,second;
        if(!ReadIdentity(worker,handleOffset,rootPrerequisite,read,true,first) ||
            !ReadIdentity(worker,handleOffset,rootPrerequisite,read,true,second))return false;
        failure_=TaskCompletionFailure::Changed;if(first!=second)return false;
        struct Pin {
            JobLeaseApi api;void* group{};
            ~Pin(){if(group)api.release(&group);}
        } pin{api,reinterpret_cast<void*>(uintptr_t(first.group))};
        api.retain(pin.group);
        if(!ReadIdentity(worker,handleOffset,rootPrerequisite,read,false,second))return false;
        Identity third;if(!ReadIdentity(worker,handleOffset,rootPrerequisite,read,false,third))return false;
        failure_=TaskCompletionFailure::Changed;if(first!=second || first!=third)return false;
        api_=api;group_=pin.group;pin.group=nullptr;handleAddress_=first.handle;captured_=true;
        failure_=TaskCompletionFailure::None;return true;
    }catch(...){failure_=TaskCompletionFailure::Read;return false;}

    // Call only after the original callback returned normally. Cancellation,
    // timeout or a zero count observed before this point is not completion.
    bool MarkOriginalReturned(){
        if(!captured_ || originalReturned_)return false;
        originalReturned_=true;return true;
    }
    bool Complete()const{return captured_ && originalReturned_ && group_ && api_.complete(group_);}
    bool Captured()const{return captured_;}
    bool OriginalReturned()const{return originalReturned_;}
    uint64_t Group()const{return reinterpret_cast<uintptr_t>(group_);}
    uint64_t HandleAddress()const{return handleAddress_;}
    TaskCompletionFailure Rejection()const{return failure_;}
private:
    struct Identity {
        uint64_t handle{},group{};
        bool operator==(const Identity&)const=default;
    };
    static bool Pointer(uint64_t value,uint64_t bytes){
        return value && !(value&7) && value<=UINT64_MAX-bytes && value<=std::numeric_limits<uintptr_t>::max();
    }
    bool ReadIdentity(uint64_t worker,uint64_t handleOffset,uint64_t prerequisite,ReadMemory read,bool beforeRetain,Identity& out){
        failure_=TaskCompletionFailure::Read;
        if(!read(worker+0x20,&out.handle,8))return false;
        failure_=TaskCompletionFailure::HandleAddress;if(out.handle!=worker+handleOffset)return false;
        failure_=TaskCompletionFailure::Read;if(!read(out.handle,&out.group,8))return false;
        failure_=TaskCompletionFailure::Group;if(!Pointer(out.group,0x20))return false;
        failure_=TaskCompletionFailure::PrerequisiteAlias;if(out.group==prerequisite)return false;
        int32_t references{},pending{};failure_=TaskCompletionFailure::Read;
        if(!read(out.group+0x1c,&references,4) || !read(out.group+0x18,&pending,4))return false;
        failure_=TaskCompletionFailure::References;
        if(references<=0 || (beforeRetain && references==std::numeric_limits<int32_t>::max()))return false;
        failure_=TaskCompletionFailure::NoCallbackToken;if(pending<=0)return false;
        return true;
    }
    JobLeaseApi api_{};void* group_{};
    uint64_t handleAddress_{};
    bool captured_{},originalReturned_{};
    TaskCompletionFailure failure_=TaskCompletionFailure::None;
};
}
