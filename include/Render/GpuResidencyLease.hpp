#pragma once
#include <d3d12.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "Render/NativeTargetLifetime.hpp"

namespace cvr::stereo::texture_read {struct ImmutableLease;}
namespace cvr::gpu::residency {
struct BorrowedTarget {
    stereo::texture_read::WritableTargetIdentity identity;
    stereo::target_lifetime::BorrowedScope lifetime;
};
enum class State:uint32_t {Empty,Acquired,Submitted,Fenced,Releasing,Retired,Quarantined};
namespace detail {
// Shared pure transaction logic. SDK callbacks are supplied by the owned lease.
class Transaction {
public:
    State Status()const noexcept{return state_;}
    size_t ObjectCount()const noexcept{return count_;}
    size_t EvictionAttempts()const noexcept{return attempted_;}
    template<class Make>HRESULT Acquire(size_t count,Make&& make){
        if(state_!=State::Empty || !count)return E_INVALIDARG;
        count_=count;HRESULT hr{};
        try{hr=make();}catch(...){state_=State::Quarantined;return E_FAIL;}
        if(SUCCEEDED(hr))state_=State::Acquired;else count_=0;
        return hr;
    }
    bool MarkSubmitted()noexcept{
        if(state_!=State::Acquired)return false;state_=State::Submitted;return true;
    }
    HRESULT AssociateFence(uint64_t value,HRESULT signalResult)noexcept{
        if(state_!=State::Submitted)return E_UNEXPECTED;
        if(FAILED(signalResult) || !value || value==UINT64_MAX){
            state_=State::Quarantined;return FAILED(signalResult)?signalResult:E_INVALIDARG;
        }
        signal_=value;state_=State::Fenced;return S_OK;
    }
    template<class Evict>HRESULT Cancel(Evict&& evict){
        if(state_!=State::Acquired)return E_ACCESSDENIED;return Release(evict);
    }
    template<class Evict>HRESULT Retire(uint64_t completed,Evict&& evict){
        if(state_==State::Submitted){state_=State::Quarantined;return E_UNEXPECTED;}
        if(state_!=State::Fenced)return E_ACCESSDENIED;
        if(completed==UINT64_MAX){state_=State::Quarantined;return DXGI_ERROR_DEVICE_REMOVED;}
        if(completed<signal_)return S_FALSE;return Release(evict);
    }
    void Quarantine()noexcept{state_=State::Quarantined;}
private:
    template<class Evict>HRESULT Release(Evict& evict){
        state_=State::Releasing;HRESULT first=S_OK;
        while(attempted_<count_){const auto index=attempted_++;HRESULT hr{};
            try{hr=evict(index);}catch(...){hr=E_FAIL;}
            if(FAILED(hr) && SUCCEEDED(first))first=hr;
        }
        state_=FAILED(first)?State::Quarantined:State::Retired;return first;
    }
    State state_=State::Empty;size_t count_{},attempted_{};uint64_t signal_{};
};
struct Storage;
}
// Owns additional SDK residency references only. The caller separately proves
// native content, state, producer ordering and the exact consumer submission.
// Calls on one Lease must be serialized by its owner.
class Lease {
public:
    Lease();~Lease();
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
    Lease(Lease&&)noexcept;Lease& operator=(Lease&&)noexcept;
    HRESULT Acquire(ID3D12Device*,std::span<const stereo::texture_read::ImmutableLease>);
    // Separate acquisition mode, never mixed with immutable material sources.
    // Native target lifetime remains the caller's same-invocation epoch proof;
    // this lease owns only additional SDK residency and COM references.
    HRESULT AcquireBorrowedTargets(ID3D12Device*,std::span<const BorrowedTarget>);
    // Must succeed immediately BEFORE ExecuteCommandLists can see the consumer.
    HRESULT MarkSubmitted(ID3D12CommandQueue*);
    // The owner serializes every member through this call and the subsequent
    // ExecuteCommandLists. Refusal leaves ALL members acquired/cancelable.
    // Only after complete preflight are the nonthrowing ledger states committed.
    static HRESULT MarkSubmittedTogether(ID3D12CommandQueue*,std::span<Lease*>);
    // Pass the actual Signal result, after that queue's consumer execution.
    // Failed/missing association quarantines; it never balances residency early.
    HRESULT AssociateFence(ID3D12CommandQueue*,ID3D12Fence*,uint64_t,HRESULT signalResult);
    HRESULT Cancel(); // Only before MarkSubmitted, with no possible GPU consumer.
    HRESULT Retire(); // S_FALSE while pending; UINT64_MAX is never completion.
    void Quarantine()noexcept;
    State Status()const noexcept;
    size_t ObjectCount()const noexcept{return objects_;}
    size_t SourceCount()const noexcept{return sources_;}
private:
    HRESULT AcquireStorage(std::unique_ptr<detail::Storage>,size_t sourceCount);
    HRESULT FinishRelease(HRESULT)noexcept;
    std::unique_ptr<detail::Storage> storage_;
    State terminal_=State::Empty;size_t objects_{},sources_{};
};
// Uncertain accounting or abandoned acquisitions disable further acquisitions.
// Quarantined pins are intentionally retained for the remainder of the process.
bool Healthy()noexcept;
uint64_t QuarantinedCount()noexcept;
}
