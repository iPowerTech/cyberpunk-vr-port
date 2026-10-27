#pragma once
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace cvr::render {
struct DescriptorTarget {uintptr_t resource{};uint32_t width{},height{};};
// Metadata only: no COM references and no GPU allocations. CPU descriptors can
// be recycled, so a replacement publishes resource and dimensions together.
// Capacity is the floor; Reserve raises it to the number of descriptor slots the engine's heaps
// actually hold. One slot is one handle, so a cache that large never has to evict a live one.
template<size_t Capacity> class DescriptorCache {
    static_assert(Capacity>0);
    struct Entry {DescriptorTarget target;typename std::list<uintptr_t>::iterator recent;};
    std::mutex mutex;
    std::list<uintptr_t> recent;
    std::unordered_map<uintptr_t,Entry> entries;
    std::unordered_set<uintptr_t> pinned;
    size_t capacity=Capacity;
public:
    void Reserve(size_t slots) {std::lock_guard lock(mutex);if(slots>capacity)capacity=slots;}
    size_t Limit(){std::lock_guard lock(mutex);return capacity;}
    void PinResources(std::span<const uintptr_t> resources) {
        std::lock_guard lock(mutex);pinned.clear();
        for(auto p:resources)if(p)pinned.insert(p);
    }
    bool Store(uintptr_t handle,DescriptorTarget target,bool* evicted=nullptr) {
        if(evicted)*evicted=false;
        if(!handle)return false;
        std::lock_guard lock(mutex);
        auto found=entries.find(handle);
        if(!target.resource) {
            if(found!=entries.end()){recent.erase(found->second.recent);entries.erase(found);}
            return true;
        }
        if(found!=entries.end()) {
            found->second.target=target;recent.splice(recent.begin(),recent,found->second.recent);return true;
        }
        if(entries.size()>=capacity) {
            auto victim=recent.end();
            for(auto it=recent.rbegin();it!=recent.rend();++it) {
                if(!pinned.contains(entries.at(*it).target.resource)){victim=std::prev(it.base());break;}
            }
            if(victim==recent.end())return false;
            entries.erase(*victim);recent.erase(victim);if(evicted)*evicted=true;
        }
        recent.push_front(handle);entries.emplace(handle,Entry{target,recent.begin()});return true;
    }
    std::optional<DescriptorTarget> Read(uintptr_t handle) {
        std::lock_guard lock(mutex);const auto it=entries.find(handle);
        if(it==entries.end())return {};
        recent.splice(recent.begin(),recent,it->second.recent);return it->second.target;
    }
    size_t Size(){std::lock_guard lock(mutex);return entries.size();}
};
}
