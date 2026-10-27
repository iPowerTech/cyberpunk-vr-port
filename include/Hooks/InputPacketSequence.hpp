#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cvr::input {
template<class Pad> class InputPacketSequence {
    static_assert(std::is_trivially_copyable_v<Pad>);
public:
    uint32_t Publish(const Pad& pad) {
        if(!m_valid || std::memcmp(&m_previous,&pad,sizeof(Pad))!=0) {
            m_previous=pad;m_valid=true;++m_sequence;
        }
        return m_sequence;
    }
private:
    Pad m_previous{};
    uint32_t m_sequence{};
    bool m_valid{};
};
}
