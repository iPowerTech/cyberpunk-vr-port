#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace cvr::capture {
// Access under the capture owner's publication mutex. A pointer is not a
// version of its pixels: readers and in-progress writers reserve the resource.
class BufferLeases {
public:
    bool BeginWrite(uintptr_t resource) {
        if(!resource)return false;
        auto& value=m_uses[resource];
        if(value.readers || value.writing)return false;
        value.writing=true;return true;
    }
    void EndWrite(uintptr_t resource) {
        const auto found=m_uses.find(resource);
        if(found==m_uses.end())return;
        found->second.writing=false;
        if(!found->second.readers)m_uses.erase(found);
    }
    bool Pin(uintptr_t resource) {
        if(!resource)return false;
        auto& value=m_uses[resource];
        if(value.writing)return false;
        ++value.readers;return true;
    }
    void Unpin(uintptr_t resource) {
        const auto found=m_uses.find(resource);
        if(found==m_uses.end())return;
        if(found->second.readers)--found->second.readers;
        if(!found->second.readers && !found->second.writing)m_uses.erase(found);
    }
    bool Writing(uintptr_t resource) const {
        const auto found=m_uses.find(resource);
        return found!=m_uses.end() && found->second.writing;
    }
private:
    struct Use { uint32_t readers{};bool writing{}; };
    std::unordered_map<uintptr_t,Use> m_uses;
};
class WriteRelease {
    BufferLeases& m_leases;
    std::mutex& m_mutex;
    uintptr_t (&m_resources)[2];
public:
    WriteRelease(BufferLeases& leases,std::mutex& mutex,uintptr_t (&resources)[2])
        :m_leases(leases),m_mutex(mutex),m_resources(resources) {}
    ~WriteRelease() {
        if(!m_resources[0] && !m_resources[1])return;
        std::lock_guard lock(m_mutex);
        for(const auto resource:m_resources)if(resource)m_leases.EndWrite(resource);
    }
};
}
