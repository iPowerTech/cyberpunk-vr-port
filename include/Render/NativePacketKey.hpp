#pragma once
#include "Render/NativePacketInputs.hpp"
#include <string>

namespace cvr::stereo::packets {
// Capture-side equivalence key for the ordinary mesh path in CP2077 2.5.
// Still NOT a runtime draw contract: pointed-to geometry/material resources and
// camera bindings require independent validation/retention by the pass owner.
// The original Packet/Inputs are never changed or fed back into the native pool.
inline bool MeshCandidateKey(Packet packet,const Inputs& input,std::span<const uint8_t> blob,std::string& output) {
    if(input.valid!=127 || input.draw[0]!=0 || !(packet.second&(uint64_t(1)<<51)) ||
       !input.instances || input.instances!=((packet.second>>18)&0x7fff) ||
       (input.source!=1 && input.source!=2) || input.instanceStride!=((packet.second&(uint64_t(1)<<50))?64u:48u) ||
       uint64_t(input.instances)*input.instanceStride!=input.instanceBytes ||
       input.instanceOffset>blob.size() || input.instanceBytes>blob.size()-input.instanceOffset ||
       input.programBytes!=input.commandProgramField || input.programOffset>blob.size() ||
       input.programBytes>blob.size()-input.programOffset)return false;
    const auto program=blob.subspan(input.programOffset,input.programBytes);
    if(!program.empty() && !ValidNativeProgram(program))return false;
    // 334CC0 discards the low 13 sort bits for state comparison; draw order is
    // retained separately by StereoMeshPlan. Keep every rendering/control bit.
    packet.first&=~((((uint64_t(1)<<39)-1)<<17)|0x1fffull);
    packet.second&=~(((uint64_t(1)<<18)-1)|(((uint64_t(1)<<17)-1)<<33));
    auto draw=input.draw;
    // Kind 0 consumers 229D0C0 / 335900 / 333C80 read +0 and +8..+2C (2.31's 1F1E44 / 1F1FA8 / 1F020C too).
    // The other bytes are native union/padding, often uninitialized stack data.
    std::fill(draw.begin()+1,draw.begin()+8,0);std::fill(draw.begin()+0x2d,draw.end(),0);
    auto state=input.state;
    // Owned program bytes replace its transient pointer at +08. Consumers 338470 / 22EED00 / 334CC0 read
    // +0..3, +8..13 and +18..1E of the 32-byte 2.5 record, not +4..7, +14..17 or +1F; +20..27 are never copied.
    std::fill(state.begin()+0x04,state.begin()+0x08,0);
    std::fill(state.begin()+StateProgram,state.begin()+StateProgram+8,0);
    std::fill(state.begin()+0x14,state.begin()+0x18,0);state[0x1f]=0;
    std::string result;
    result.reserve(sizeof(packet)+draw.size()+input.shaders.size()+state.size()+input.object.size()+
        sizeof(input.instanceStride)+sizeof(input.programBytes)+program.size()+input.instanceBytes);
    auto add=[&](const void* data,size_t bytes){if(bytes)result.append(static_cast<const char*>(data),bytes);};
    add(&packet,sizeof(packet));add(draw.data(),draw.size());add(input.shaders.data(),input.shaders.size());
    add(state.data(),state.size());add(input.object.data(),input.object.size());
    add(&input.instanceStride,sizeof(input.instanceStride));add(&input.programBytes,sizeof(input.programBytes));
    add(program.data(),program.size());add(blob.data()+input.instanceOffset,input.instanceBytes);
    output=std::move(result);return true;
}
}
