#pragma once
#include <cstdint>

namespace cvr::stereo::constants {
// Advisory, one-use LRU touch only. It owns no pointers and never authorizes
// reading mapped bytes; reads must resolve a fresh owned registry entry.
class UploadTouchReceipt {
    uint64_t base_{},bytes_{},generation_{};
public:
    void Clear(){base_=bytes_=generation_=0;}
    bool Set(uint64_t base,uint64_t bytes,uint64_t generation){
        Clear();
        if(!base || !bytes || !generation || bytes>UINT64_MAX-base)return false;
        base_=base;bytes_=bytes;generation_=generation;return true;
    }
    bool Take(uint64_t address,uint64_t bytes,uint64_t generation){
        const auto base=base_,size=bytes_,epoch=generation_;Clear();
        return epoch && epoch==generation && bytes && bytes<=65536 && address>=base &&
            address-base<=size && bytes<=size-(address-base);
    }
};
}
