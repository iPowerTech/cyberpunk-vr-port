#include "Render/NativeCommandRecording.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::native_recording;
static void Require(bool value){if(!value)throw std::runtime_error("native recording contract assertion failed");}
int main()try{
    detail::SubmissionState first;
    Require(first.Fresh() && !first.Validating());
    Require(!first.BeforeSubmit(0,true) && first.Fresh());
    Require(first.BeforeSubmit(1,true) && !first.Fresh() && first.Validating());
    first.Submitted();Require(!first.Validating() && first.submitted && !first.pending && !first.rejected);
    Require(!first.BeforeSubmit(1,true) && first.rejected);first.Submitted();Require(!first.Fresh() && !first.Validating());

    for(const bool native:{false,true}){
        detail::SubmissionState invalid;
        Require(!invalid.BeforeSubmit(native?2u:1u,native) && invalid.rejected && invalid.pending==1);
        invalid.Submitted();Require(invalid.submitted && !invalid.pending && !invalid.Fresh());
    }
    detail::SubmissionState concurrent;
    Require(concurrent.BeforeSubmit(1,true));Require(!concurrent.BeforeSubmit(1,true));
    Require(concurrent.pending==2 && concurrent.rejected);concurrent.Submitted();
    Require(concurrent.pending==1 && !concurrent.Validating());concurrent.Submitted();Require(!concurrent.pending);
    // Reset creates a separate token; it never clears the old token's rejection.
    detail::SubmissionState replacement;Require(replacement.Fresh() && first.rejected);
    replacement.Submitted();Require(replacement.Fresh());
    Require(replacement.BeforeSubmit(1,true) && replacement.Validating() && !first.Validating());

    constexpr uint64_t base=0x140000000;
    Require(detail::Caller(base,base+detail::ResetReturn,detail::ResetReturn));
    Require(detail::Caller(base,base+detail::ExecuteReturn,detail::ExecuteReturn));
    Require(!detail::Caller(base,base+detail::ExecuteReturn+1,detail::ExecuteReturn));
    Require(!detail::Caller(0,detail::ExecuteReturn,detail::ExecuteReturn));
    Require(!detail::Caller(UINT64_MAX-1,1,detail::ExecuteReturn));
    constexpr uint64_t imageBytes=0x5000000,foreign=0x7ffe00000100,expected=base+detail::ExecuteReturn;
    Require(detail::ExecuteOrigin(base,imageBytes,expected,{}));
    Require(!detail::ExecuteOrigin(base,imageBytes,foreign,{}));
    const std::array<uint64_t,4> wrapped{foreign+8,foreign+16,expected,base+0x91F078};
    Require(detail::ExecuteOrigin(base,imageBytes,foreign,wrapped));
    auto wrong=wrapped;wrong[2]=expected+1;
    Require(!detail::ExecuteOrigin(base,imageBytes,foreign,wrong));
    wrong[3]=expected;Require(!detail::ExecuteOrigin(base,imageBytes,foreign,wrong));
    Require(!detail::ExecuteOrigin(base,imageBytes,base+0x1234,wrapped));
    const std::array<uint64_t,3> absent{foreign,0,base+imageBytes};
    Require(!detail::ExecuteOrigin(base,imageBytes,foreign,absent));
    const std::array<uint64_t,2> edge{base+imageBytes,expected};
    Require(detail::ExecuteOrigin(base,imageBytes,foreign,edge));
    const std::array<uint64_t,2> wrongEdge{base,expected};
    Require(!detail::ExecuteOrigin(base,imageBytes,foreign,wrongEdge));
    std::array<uint64_t,detail::MaxUnwoundFrames> bounded{};bounded.fill(foreign);bounded.back()=expected;
    Require(detail::ExecuteOrigin(base,imageBytes,foreign,bounded));
    std::array<uint64_t,detail::MaxUnwoundFrames+1> tooDeep{};tooDeep.fill(foreign);tooDeep.back()=expected;
    Require(!detail::ExecuteOrigin(base,imageBytes,foreign,tooDeep));
    Require(!detail::ExecuteOrigin(0,imageBytes,expected,wrapped));
    Require(!detail::ExecuteOrigin(base,detail::ExecuteReturn,expected,wrapped));
    Require(!detail::ExecuteOrigin(UINT64_MAX-16,imageBytes,expected,wrapped));
    cvr::stereo::camera_upload::Context context{0x1000,0x2000,0x3000};
    Require(detail::ContextMatches(context,context.list,context.list,context.state,1,0));
    Require(detail::ContextMatches(context,context.list,context.list,context.state,2,0));
    for(const UINT kind:{0u,3u,4u,UINT_MAX})Require(!detail::ContextMatches(context,context.list,context.list,context.state,kind,0));
    Require(!detail::ContextMatches(context,context.list+8,context.list,context.state,1,0));
    Require(!detail::ContextMatches(context,context.list,context.list+8,context.state,1,0));
    Require(!detail::ContextMatches(context,context.list,context.list,context.state+8,1,0));
    Require(!detail::ContextMatches(context,context.list,context.list,context.state,1,1));
    Require(!detail::ContextMatches({},context.list,context.list,context.state,1,0));
    const std::array<uint8_t,3> bytes{'a','b','c'};
    Require(detail::Fingerprint(bytes)==0xE71FA2190541574Bull);
    auto changed=bytes;changed[1]^=1;Require(detail::Fingerprint(changed)!=detail::Fingerprint(bytes));
    Require(detail::Contract.size()==11);
    bool resetCovered=false,submitCovered=false,bulk=false,split=false,returnOwner=false;
    for(size_t i=0;i<detail::Contract.size();++i){const auto code=detail::Contract[i];
        Require(code.rva && code.bytes && code.bytes<=2048 && code.hash);
        for(size_t j=0;j<i;++j)Require(detail::Contract[j].rva!=code.rva);
        resetCovered|=code.rva<=detail::ResetReturn && uint64_t(code.rva)+code.bytes>detail::ResetReturn;
        submitCovered|=code.rva<=detail::ExecuteReturn && uint64_t(code.rva)+code.bytes>detail::ExecuteReturn;
        bulk|=code.rva==0x1F5E5C;split|=code.rva==0x1E3D550;returnOwner|=code.rva==0x1FA82C;
    }
    Require(resetCovered && submitCovered && bulk && split && returnOwner);
    std::puts("PASS native recording provenance, nearest unwound game caller, fingerprint coverage, one-shot submissions and reset generations");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
