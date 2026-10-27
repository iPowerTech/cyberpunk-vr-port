#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace cvr::stereo::resource_replay {
inline constexpr uint32_t Slots=220,RowsPerSlot=500,MaxRows=Slots*RowsPerSlot;
using RawDeclaration=std::array<uint8_t,48>;
struct Identity {
    uint64_t consumerId{},epoch{},generation{},pool{},sourceGraph{},executionGraph{};
    bool operator==(const Identity&)const=default;
};
struct NodeBinding {
    uint32_t handle{},ordinal{},slot{},viewIndex{};
    bool operator==(const NodeBinding&)const=default;
};
enum class Kind13Policy:uint32_t {RecordRequestDifference,RequireSameRequest};
enum class Failure:uint32_t {
    None,Arguments,AlreadyAttempted,Identity,Changed,Nodes,Counts,Rows,Kind,Slot,NotCaptured,
    Invocation,Mode,Cursor,Busy,Command,KindMismatch,KeyMismatch,PayloadMismatch,Token,
    AlreadyCompleted,Result,Capacity,Allocation
};
struct Token {
    Identity identity;NodeBinding node;
    uint64_t nonce{};
    uint32_t index{},kind{};
    uint8_t compiledDecision{},requestedDecision{};
    bool requestDifference{};
    bool operator==(const Token&)const=default;
};

inline uint32_t PayloadBytes(uint32_t kind)noexcept{
    switch(kind){
    case 0:case 3:case 5:case 6:case 10:return 0;
    case 1:return 40;
    case 2:return 16;
    case 4:case 7:case 8:case 11:case 12:return 4;
    case 9:case 13:return 1;
    default:return UINT32_MAX;
    }
}
inline uint32_t CommandBytes(uint32_t kind)noexcept{
    const auto bytes=PayloadBytes(kind);return bytes==UINT32_MAX?0:8+bytes;
}

// Pure copied grammar/cursor receipt for guarded 1F0ED0/1F0F80. Rows are ALL
// per-slot declarations, flattened in slot/index order, not only serialized use
// IDs. Root supplies two coherent pre-compiler observations and frozen graph
// identity. No native pointers are followed, no native counters changed, and no
// allocator lifetime, access direction, alias safety or GPU completion is proved.
// Caller serializes methods. Original native emitter must still run exactly once.
class Receipt {
public:
    Receipt()=default;
    Receipt(const Receipt&)=delete;
    Receipt& operator=(const Receipt&)=delete;
    Receipt(Receipt&&)=delete;
    Receipt& operator=(Receipt&&)=delete;

    bool Capture(const Identity& before,const Identity& after,std::span<const NodeBinding> nodes,
                 std::span<const uint32_t> countsBefore,std::span<const uint32_t> countsAfter,
                 std::span<const RawDeclaration> rowsBefore,std::span<const RawDeclaration> rowsAfter,
                 Kind13Policy policy=Kind13Policy::RecordRequestDifference)try{
        if(failed_)return false;
        if(attempted_)return Reject(Failure::AlreadyAttempted);
        attempted_=true;
        if(!ValidIdentity(before))return Reject(Failure::Identity);
        if(before!=after)return Reject(Failure::Changed);
        if(policy!=Kind13Policy::RecordRequestDifference && policy!=Kind13Policy::RequireSameRequest)
            return Reject(Failure::Arguments);
        if(nodes.empty() || nodes.size()>Slots || countsBefore.size()!=Slots || countsAfter.size()!=Slots)
            return Reject(Failure::Counts);
        if(!std::equal(countsBefore.begin(),countsBefore.end(),countsAfter.begin()))return Reject(Failure::Changed);
        std::array<bool,Slots> ordinals{},used{};
        for(const auto& node:nodes){
            if(node.handle>=65536 || node.ordinal>=nodes.size() || node.slot>=Slots || node.viewIndex>1 ||
                ordinals[node.ordinal] || used[node.slot])
                return Reject(Failure::Nodes);
            ordinals[node.ordinal]=true;used[node.slot]=true;
        }
        // Input order need not equal ordinal order; handles must be globally unique.
        for(size_t i=0;i<nodes.size();++i)for(size_t j=i+1;j<nodes.size();++j)
            if(nodes[i].handle==nodes[j].handle)return Reject(Failure::Nodes);
        uint32_t total{};
        for(uint32_t slot=0;slot<Slots;++slot){
            if(countsBefore[slot]>RowsPerSlot || (countsBefore[slot] && !used[slot]))return Reject(Failure::Counts);
            starts_[slot]=total;total+=countsBefore[slot];
        }
        if(total>MaxRows || rowsBefore.size()!=total || rowsAfter.size()!=total)return Reject(Failure::Rows);
        if(!std::equal(rowsBefore.begin(),rowsBefore.end(),rowsAfter.begin()))return Reject(Failure::Changed);
        for(uint32_t slot=0;slot<Slots;++slot)for(uint32_t index=0;index<countsBefore[slot];++index){
            const auto& row=rowsBefore[starts_[slot]+index];
            if(PayloadBytes(row[0])==UINT32_MAX)return Reject(Failure::Kind);
            if(row[1]!=slot)return Reject(Failure::Slot);
        }
        rows_.assign(rowsBefore.begin(),rowsBefore.end());nodes_.assign(nodes.begin(),nodes.end());
        std::copy(countsBefore.begin(),countsBefore.end(),counts_.begin());
        identity_=before;policy_=policy;captured_=true;failure_=Failure::None;return true;
    }catch(...){rows_.clear();nodes_.clear();return Reject(Failure::Allocation);}

    // Called BEFORE the original increments native slot+40. A small command may
    // contain only 8+PayloadBytes(kind) readable bytes. Padding2/3 and inactive
    // payload bytes are never read as semantic equality.
    bool Before(const Identity& identity,const NodeBinding& node,uint32_t mode,uint32_t oldCursor,
                std::span<const uint8_t> command,Token& output)noexcept{
        output={};if(failed_)return false;
        if(!captured_)return Reject(Failure::NotCaptured);
        if(identity!=identity_)return Reject(Failure::Invocation);
        if(!MatchesNode(node))return Reject(Failure::Nodes);
        if(mode!=2)return Reject(Failure::Mode);
        const auto slot=node.slot;
        if(inFlight_[slot])return Reject(Failure::Busy);
        if(oldCursor!=cursors_[slot] || oldCursor>=counts_[slot])return Reject(Failure::Cursor);
        if(command.size()<8)return Reject(Failure::Command);
        const auto kind=command[0];const auto bytes=PayloadBytes(kind);
        if(bytes==UINT32_MAX)return Reject(Failure::Kind);
        if(command.size()<8+bytes)return Reject(Failure::Command);
        const auto& expected=rows_[starts_[slot]+oldCursor];
        if(kind!=expected[0])return Reject(Failure::KindMismatch);
        if(command[1]!=slot)return Reject(Failure::Slot);
        if(!std::equal(command.begin()+4,command.begin()+8,expected.begin()+4))return Reject(Failure::KeyMismatch);
        const bool requestDifference=kind==13 && command[8]!=expected[8];
        if(kind!=13 || policy_==Kind13Policy::RequireSameRequest){
            // Guarded descriptor readers consume only bits0..2 at TEXTURE+32;
            // BUFFER ignores +32..35 entirely. Both omit trailing +38..39
            // padding. Only fresh requests use these masks, never stored rows.
            const bool buffer=kind==1 && command[44]==1 && expected[44]==1;
            const bool texture=kind==1 && command[44]==0 && expected[44]==0;
            // Kind2 stores DWORD source-key, then QWORD callback: +12..15 is
            // uninitialized alignment padding, not a wider source identifier.
            const bool equal=kind==2?
                std::equal(command.begin()+8,command.begin()+12,expected.begin()+8) &&
                std::equal(command.begin()+16,command.begin()+24,expected.begin()+16):buffer || texture?
                std::equal(command.begin()+8,command.begin()+40,expected.begin()+8) &&
                (!texture || (command[40]&7)==(expected[40]&7)) &&
                std::equal(command.begin()+44,command.begin()+46,expected.begin()+44):
                std::equal(command.begin()+8,command.begin()+8+bytes,expected.begin()+8);
            if(!equal)return Reject(Failure::PayloadMismatch);
        }
        if(nextNonce_==UINT64_MAX)return Reject(Failure::Capacity);
        Token token{identity_,node,++nextNonce_,oldCursor,kind,kind==13?expected[8]:uint8_t{},
            kind==13?command[8]:uint8_t{},requestDifference};
        pending_[slot]=token;inFlight_[slot]=true;output=token;return true;
    }

    // Native result is AL. Only kind13 has the audited recorded-decision result;
    // other kinds' native return values are not guessed as success/visibility.
    bool After(const Token& token,const Identity& identity,const NodeBinding& node,uint32_t cursorAfter,
               uint8_t nativeResult)noexcept{
        if(!captured_)return Reject(Failure::NotCaptured);
        if(token.node.slot>=Slots || !inFlight_[token.node.slot] || token!=pending_[token.node.slot])return Reject(Failure::Token);
        const auto slot=token.node.slot;inFlight_[slot]=false;pending_[slot]={};
        if(failed_)return false;
        if(identity!=identity_ || token.identity!=identity_ || node!=token.node)return Reject(Failure::Invocation);
        if(cursorAfter!=token.index+1)return Reject(Failure::Cursor);
        if(token.kind==13 && nativeResult!=token.compiledDecision)return Reject(Failure::Result);
        cursors_[slot]=cursorAfter;++completed_;
        requestDifferences_+=token.requestDifference;return true;
    }
    bool Complete()const noexcept{
        if(!captured_ || failed_ || completed_!=rows_.size())return false;
        for(uint32_t slot=0;slot<Slots;++slot)if(inFlight_[slot] || cursors_[slot]!=counts_[slot])return false;
        return true;
    }
    bool Captured()const noexcept{return captured_;}
    bool Failed()const noexcept{return failed_;}
    Failure Rejection()const noexcept{return failure_;}
    const Identity& Binding()const noexcept{return identity_;}
    bool MatchesIdentity(const Identity& identity)const noexcept{return captured_ && !failed_ && identity==identity_;}
    bool MatchesNode(const NodeBinding& node)const noexcept{
        return captured_ && std::find(nodes_.begin(),nodes_.end(),node)!=nodes_.end();
    }
    uint64_t CompletedCalls()const noexcept{return completed_;}
    uint64_t RequestDifferences()const noexcept{return requestDifferences_;}
    uint32_t Cursor(uint32_t slot)const noexcept{return slot<Slots?cursors_[slot]:0;}
    uint32_t DeclaredCount(uint32_t slot)const noexcept{return slot<Slots?counts_[slot]:0;}
    bool Pending(uint32_t slot)const noexcept{return slot<Slots && inFlight_[slot];}
    const std::vector<RawDeclaration>& Declarations()const noexcept{return rows_;}
private:
    static bool Pointer(uint64_t value,uint64_t bytes)noexcept{
        return value && !(value&7) && value<=UINT64_MAX-bytes && value<=std::numeric_limits<uintptr_t>::max();
    }
    static bool ValidIdentity(const Identity& identity)noexcept{
        return identity.consumerId && identity.epoch && identity.epoch<=UINT32_MAX && identity.generation &&
            Pointer(identity.pool,3) && Pointer(identity.sourceGraph,0x70) && Pointer(identity.executionGraph,0x70) &&
            identity.sourceGraph!=identity.executionGraph;
    }
    bool Reject(Failure reason)noexcept{if(!failed_)failure_=reason;failed_=true;return false;}
    Identity identity_;Kind13Policy policy_=Kind13Policy::RecordRequestDifference;
    std::vector<RawDeclaration> rows_;std::vector<NodeBinding> nodes_;
    std::array<uint32_t,Slots> counts_{},starts_{},cursors_{};
    std::array<Token,Slots> pending_{};
    std::array<bool,Slots> inFlight_{};
    uint64_t nextNonce_{},completed_{},requestDifferences_{};
    bool attempted_{},captured_{},failed_{};Failure failure_=Failure::None;
};
}
