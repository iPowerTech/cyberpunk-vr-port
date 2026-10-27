#pragma once
#include <array>
#include <cstdint>

namespace cvr::stereo {
struct CameraWordMask {
    std::array<uint64_t,4> bits{};
    static CameraWordMask All() {return {{{~uint64_t(0),~uint64_t(0),~uint64_t(0),(uint64_t(1)<<20)-1}}};}
    void Exclude(unsigned word) {if(word<212)bits[word/64]&=~(uint64_t(1)<<(word%64));}
    bool Contains(const CameraWordMask& required) const {
        for(unsigned i=0;i<4;++i)if(required.bits[i]&~bits[i])return false;
        return true;
    }
};
}
