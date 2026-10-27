#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>
struct ID3D12Resource;
namespace cvr::stereo::packets {
// Own only bytes observed in a CPU upload. Unwritten holes and GPU-to-GPU
// overwrites cannot be read as zeros or as an earlier copy's contents.
class UploadShadow {
public:
    static constexpr size_t Limit=16*1024*1024;
    void Reset(size_t bytes=0){const auto next=bytes<=Limit?bytes:0;
        if(!next || next!=size_)std::vector<uint8_t>().swap(data_);ranges_.clear();size_=next;}
    bool Write(size_t at,std::span<const uint8_t> bytes){
        if(at>size_ || bytes.size()>size_-at)return false;
        if(bytes.empty())return true;
        if(data_.empty())data_.resize(size_);
        std::memcpy(data_.data()+at,bytes.data(),bytes.size());
        ranges_.push_back({at,at+bytes.size()});std::sort(ranges_.begin(),ranges_.end());
        size_t n{};for(const auto& r:ranges_){if(n && r.first<=ranges_[n-1].second)ranges_[n-1].second=std::max(r.second,ranges_[n-1].second);else ranges_[n++]=r;}ranges_.resize(n);return true;
    }
    void Invalidate(size_t at,size_t bytes){
        if(at>size_ || bytes>size_-at){ranges_.clear();return;}
        std::vector<std::pair<size_t,size_t>> kept;
        for(auto r:ranges_){if(r.second<=at || r.first>=at+bytes)kept.push_back(r);
            else {if(r.first<at)kept.push_back({r.first,at});if(r.second>at+bytes)kept.push_back({at+bytes,r.second});}}
        ranges_=std::move(kept);
    }
    bool Read(size_t at,void* output,size_t bytes)const{
        if(!output || at>size_ || bytes>size_-at)return false;
        for(const auto& r:ranges_)if(at>=r.first && at+bytes<=r.second){std::memcpy(output,data_.data()+at,bytes);return true;}
        return false;
    }
private:
    size_t size_{};std::vector<uint8_t> data_;std::vector<std::pair<size_t,size_t>> ranges_;
};
namespace upload_shadow {
constexpr uint64_t AddressTag=0xFFFFFFFF00000000ull;
void Watch(uint64_t gpu,uint64_t resource,uint64_t bytes);
void Stop();
bool Active();
void Invalidate(ID3D12Resource* destination);
bool Read(uint64_t gpu,uint64_t resource,uint64_t offset,void* output,size_t bytes);
void Observe(ID3D12Resource* destination,uint64_t offset,ID3D12Resource* source,uint64_t sourceOffset,uint64_t bytes);
}
}
