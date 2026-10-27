#pragma once
#include "Render/NativeStereoCameraUpload.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <climits>
#include <cstdint>
#include <span>

namespace cvr::stereo::native_recording {
namespace detail {
inline constexpr uint32_t ResetReturn=0x1FDA14,ExecuteReturn=0x1F5DDF;
struct Code {uint32_t rva,bytes;uint64_t hash;};
// Acquire/reset, handle ownership transfer, bulk/single submit, split fallback,
// context return and queue selection. Recursive submits use disjoint halves.
inline constexpr std::array<Code,11> Contract{{
    {0x1FD49C,1789,0x63D0EEFEAC6254D8ull},{0x1FB564,333,0x589B9E0C5E74800Full},
    {0x1FAFA4,1470,0x3541631D8D75A2E6ull},{0x1FAD8C,536,0xFCAA595AD3D88B0Aull},
    {0x1F5E5C,1267,0xE8EDEEEE241BE7B7ull},{0x91EED4,641,0x5E94D16A5DBECA40ull},
    {0x1F5BB0,682,0x133EED36FCDED78Dull},{0x1E3D550,206,0xB6958970A8471F84ull},
    {0x1FA82C,422,0xAE057F3A739A8BE1ull},{0x1FA9D4,167,0x8E908047FAA1CEB6ull},
    {0x91F240,77,0x9E7D6243F6F033C4ull}
}};
inline uint64_t Fingerprint(std::span<const uint8_t> bytes){
    uint64_t hash=14695981039346656037ull;for(const auto byte:bytes)hash=(hash^byte)*1099511628211ull;return hash;
}
inline bool Caller(uint64_t base,uint64_t address,uint32_t rva){
    return base && base<=UINT64_MAX-rva && address==base+rva;
}
inline constexpr size_t MaxUnwoundFrames=64;
// Frames must come from the OS unwinder, in nearest-to-farthest order.
inline bool ExecuteOrigin(uint64_t base,uint64_t imageBytes,uint64_t caller,std::span<const uint64_t> frames){
    if(!base || imageBytes<=ExecuteReturn || imageBytes>UINT64_MAX-base || frames.size()>MaxUnwoundFrames)return false;
    const auto inGame=[&](uint64_t address){return address>=base && address-base<imageBytes;};
    if(inGame(caller))return Caller(base,caller,ExecuteReturn);
    for(const auto frame:frames)if(inGame(frame))return Caller(base,frame,ExecuteReturn);
    return false;
}
inline bool ContextMatches(camera_upload::Context expected,uint64_t list,uint64_t actualList,uint64_t state,UINT kind,uint64_t submitted){
    return expected && list && expected.list==list && actualList==list && expected.state==state &&
        (kind==1 || kind==2) && !submitted;
}
struct SubmissionState {
    UINT pending{};bool submitted{},rejected{};
    bool Fresh()const{return !pending && !submitted && !rejected;}
    bool BeforeSubmit(UINT occurrences,bool native){
        if(!occurrences)return false;
        const bool first=Fresh();if(pending!=UINT_MAX)++pending;else rejected=true;
        if(!first || occurrences!=1 || !native)rejected=true;
        return Validating();
    }
    bool Validating()const{return pending==1 && !submitted && !rejected;}
    void Submitted(){if(pending){--pending;submitted=true;}}
};
}
struct Lease {
    Microsoft::WRL::ComPtr<IUnknown> token;
    uint64_t generation{},context{},list{},registry{};
};
// This is an audited native ownership contract, not GPU-safe support for replay
// through a foreign/manual caller. It must be acquired before recording probes.
bool Compatible();
void Reset(ID3D12GraphicsCommandList*,uintptr_t caller);
bool Capture(ID3D12GraphicsCommandList*,camera_upload::Context,Lease&);
// Read-only validation at the current native recording position. No recapture,
// token mutation, native resource-use registration or submission change.
bool ValidateRecording(const Lease&,ID3D12GraphicsCommandList*,camera_upload::Context);
void BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*,uintptr_t caller);
bool ValidateSubmission(const Lease&,ID3D12CommandQueue*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
// 1 arguments, 2 fingerprints, 3 missing token, 4 reset origin, 5 generation,
// 6 native context, 7 registry, 8 submit origin/queue, 9 replay, 10 token storage,
// 11 allocation/exception, 12 not pending.
uint32_t LastFailure();
// Execute caller, expected caller, actual/expected queue, occurrences,
// pending count | submitted<<32 | rejected<<33, Reset caller, generation.
std::array<uint64_t,8> LastDetails();
}
