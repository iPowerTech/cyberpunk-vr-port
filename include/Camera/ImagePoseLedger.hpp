#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cvr::camera {
// Command-list recording and submission are separate publications. A reset
// discards unsubmitted metadata, just as it discards the recorded GPU commands.
template<class Payload>
class ImagePoseLedger {
public:
    struct Image {
        uintptr_t resource{},queue{};
        uint64_t generation{},stampMs{};
        Payload pose{};
        explicit operator bool() const { return generation!=0; }
    };
    void Reset(uintptr_t list) {
        std::lock_guard lock(m_mutex);m_lists[list].clear();
    }
    void Record(uintptr_t list,uintptr_t resource,const Payload& pose,bool stable) {
        if(!list || !resource)return;
        std::lock_guard lock(m_mutex);
        const auto [life,created]=m_lifetimes.try_emplace(resource,0);
        if(created)life->second=++m_lifetime;
        auto& writes=m_lists[list];
        // Keep the last write to each target in this list. Unknown pose is
        // explicit: it must revoke an earlier label when pixels are replaced.
        for(auto it=writes.begin();it!=writes.end();++it)if(it->resource==resource) { writes.erase(it);break; }
        writes.push_back({resource,pose,stable,life->second});
    }
    void Submit(uintptr_t queue,uintptr_t list,uint64_t stampMs) {
        std::lock_guard lock(m_mutex);
        const auto found=m_lists.find(list);
        if(found==m_lists.end())return;
        for(const auto& write:found->second) {
            const auto life=m_lifetimes.find(write.resource);
            if(life==m_lifetimes.end() || life->second!=write.lifetime)continue;
            Image image{write.resource,queue,++m_generation,stampMs,write.pose};
            m_images.insert_or_assign(write.resource,image);
            if(write.stable)m_latestStable=image;
        }
    }
    Image Read(uintptr_t resource) const {
        std::lock_guard lock(m_mutex);const auto found=m_images.find(resource);
        return found==m_images.end() ? Image{} : found->second;
    }
    Image LatestStable() const { std::lock_guard lock(m_mutex);return m_latestStable; }
    void ForgetResource(uintptr_t resource) {
        std::lock_guard lock(m_mutex);m_images.erase(resource);
        m_lifetimes.erase(resource);
        if(m_latestStable.resource==resource)m_latestStable={};
    }
    void ForgetList(uintptr_t list) { std::lock_guard lock(m_mutex);m_lists.erase(list); }
private:
    struct Write { uintptr_t resource;Payload pose;bool stable;uint64_t lifetime; };
    mutable std::mutex m_mutex;
    std::unordered_map<uintptr_t,std::vector<Write>> m_lists;
    std::unordered_map<uintptr_t,Image> m_images;
    std::unordered_map<uintptr_t,uint64_t> m_lifetimes;
    Image m_latestStable{};
    uint64_t m_generation{},m_lifetime{};
};
}
