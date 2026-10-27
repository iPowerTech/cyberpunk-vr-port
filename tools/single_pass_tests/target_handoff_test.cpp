#include "Render/NativeTargetHandoff.hpp"
#include "Render/NativeTextureRead.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::target_handoff;
static void Require(bool value){if(!value)throw std::runtime_error("native target handoff assertion failed");}
static detail::TargetState Depth(){detail::TargetState state;Require(state.Seed(2,D3D12_RESOURCE_STATE_DEPTH_WRITE));return state;}
static detail::RecordingState Recording(){detail::RecordingState state;Require(state.Begin(100,7));return state;}
int main()try{
    constexpr auto all=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    constexpr auto none=D3D12_RESOURCE_BARRIER_FLAG_NONE;
    constexpr auto depth=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    constexpr auto copy=D3D12_RESOURCE_STATE_COPY_SOURCE;
    const auto read=uint32_t(D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    auto state=Depth();
    Require(state.Transition(all,depth,copy,none));
    Require(state.states[0].current==copy && state.states[1].current==copy);
    Require(state.Transition(all,copy,depth,none));
    Require(state.Transition(0,depth,read,none));
    Require(state.states[0].current==read && state.states[1].current==depth);
    Require(!state.Transition(all,depth,copy,none) && state.failure==Failure::StateBefore);
    Require(state.states[0].current==read && state.states[1].current==depth);
    state.Freeze();Require(!(state.states[0].flags&FrozenKnown));

    state=Depth();Require(!state.Transition(2,depth,copy,none) && state.failure==Failure::Subresource);
    for(const auto split:{D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY,D3D12_RESOURCE_BARRIER_FLAG_END_ONLY}){
        state=Depth();Require(!state.Transition(all,depth,copy,split) && state.failure==Failure::SplitBarrier);
    }
    state=Depth();Require(!state.Transition(all,depth,0x80000000u,none) && state.failure==Failure::UnknownBarrier);
    Require(!detail::StateKnown(D3D12_RESOURCE_STATE_DEPTH_WRITE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    Require(detail::StateKnown(0) && detail::StateKnown(read));
    Require(!state.Seed(0,depth) && !state.Seed(6,depth) && !state.Seed(1,copy));
    state={};Require(!state.Transition(all,depth,copy,none));

    // Depth and stencil each have a separate ledger entry, including private
    // proof copies whose restore barriers run after the retained interval exit.
    state=Depth();auto recording=Recording();recording.Exit(9,uint32_t(ExitKind::Transition));
    recording.Touch(100,false);Require(!(recording.flags&Refused));
    Require(state.Transition(all,depth,copy,none));
    Require(state.Transition(all,copy,depth,none));
    Require(state.Transition(0,depth,read,none));
    Require(state.Transition(1,depth,D3D12_RESOURCE_STATE_DEPTH_READ,none));
    Require(recording.Close(S_OK));state.Freeze();
    Require(state.states[0].frozen==read && state.states[1].frozen==D3D12_RESOURCE_STATE_DEPTH_READ);
    Require(state.states[0].transitions==3 && state.states[1].transitions==3);
    Require(recording.Pending(200,1,true));
    Require(!(recording.flags&SubmittedObserved) && (recording.flags&PrefixUnknown));
    recording.Submitted(201);Require(!(recording.flags&SubmittedObserved));
    recording.Submitted(200);Require(recording.flags&SubmittedObserved);
    recording.Reset();Require(!(recording.flags&Refused));
    recording.Touch(100,true);Require(!(recording.flags&Refused));
    recording.Touch(101,true);Require(recording.failure==Failure::None && recording.foreignFailure==Failure::OtherListTouch);
    Require(!(recording.flags&Refused) && (recording.AggregateFlags()&Refused) && !(recording.AggregateFlags()&PendingValidated));

    // Empty or disjoint OM bindings cannot exclude a pixel-shader UAV write.
    detail::TargetState color;Require(color.Seed(1,D3D12_RESOURCE_STATE_RENDER_TARGET));
    recording=Recording();recording.Exit(9,uint32_t(ExitKind::Transition));
    Require(color.Transition(all,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,none));
    recording.ShaderDraw(100,color);
    Require((recording.flags&Refused) && !(recording.flags&CloseSucceeded) && recording.failure==Failure::UnknownCommand);
    Require(!recording.Close(S_OK));
    Require(color.Seed(1,D3D12_RESOURCE_STATE_RENDER_TARGET));
    recording=Recording();recording.Exit(9,uint32_t(ExitKind::Transition));
    recording.ShaderDraw(100,color);Require(!(recording.flags&Refused) && recording.Close(S_OK));
    recording=Recording();recording.ShaderDraw(101,color);Require(!(recording.flags&Refused));
    Require(color.Transition(all,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,none));
    recording.ShaderDraw(101,color);Require(!(recording.flags&Refused) && recording.foreignFailure==Failure::OtherListTouch);
    Require((recording.AggregateFlags()&Refused) && recording.AggregateFailure()==Failure::OtherListTouch);
    recording=Recording();
    recording.Exit(1,uint32_t(ExitKind::Close));Require(recording.Close(S_OK));Require(recording.Pending(200,1,true));
    color.Freeze();recording.Submitted(200);recording.ShaderDraw(100,color);Require(!(recording.flags&Refused));
    auto reused=recording;
    recording.ShaderDraw(101,color);Require(!(recording.flags&Refused) && recording.foreignFailure==Failure::OtherListTouch);
    reused.ShaderDraw(100,color,true);Require(!(reused.flags&Refused) && reused.foreignFailure==Failure::OtherListTouch);
    Require(color.states[0].frozen==D3D12_RESOURCE_STATE_UNORDERED_ACCESS && (color.states[0].flags&FrozenKnown));

    recording=Recording();recording.Touch(100,true);Require(!(recording.flags&Refused));
    recording.Exit(4,uint32_t(ExitKind::Copy));recording.Touch(100,true);
    Require(recording.failure==Failure::PostExitWrite && !recording.Close(S_OK));
    recording=Recording();recording.Touch(101,false);Require(recording.failure==Failure::None && recording.foreignFailure==Failure::OtherListTouch);
    recording=Recording();recording.Touch(100,false,true);Require(recording.failure==Failure::Aliasing);
    recording=Recording();Require(!recording.Match(100,8) && recording.failure==Failure::Generation);
    recording=Recording();Require(!recording.Match(101,7) && recording.failure==Failure::Generation);
    recording=Recording();recording.Exit(1,uint32_t(ExitKind::Close));
    Require(!recording.Close(E_FAIL) && recording.failure==Failure::CloseFailed);
    Require(!recording.Close(S_OK) && !recording.Pending(200,1,true));
    recording=Recording();Require(!recording.Close(S_OK) && recording.failure==Failure::MissingExit);
    recording=Recording();recording.Exit(1,1);Require(recording.Close(S_OK));recording.Touch(100,false);
    Require(recording.failure==Failure::AfterClose);
    recording=Recording();recording.Reset();Require(recording.failure==Failure::ResetBeforeSubmit);
    Require((recording.flags&ResetBeforeSubmit) && !recording.Pending(200,1,true));
    recording=Recording();recording.Exit(1,1);Require(recording.Close(S_OK));Require(recording.Pending(200,1,true));
    recording.Reset();recording.Submitted(200);Require(!(recording.flags&SubmittedObserved));
    for(const auto occurrences:{0u,2u}){
        recording=Recording();recording.Exit(1,1);Require(recording.Close(S_OK));
        Require(!recording.Pending(200,occurrences,true));
    }
    recording=Recording();recording.Exit(1,1);Require(recording.Close(S_OK));Require(!recording.Pending(200,1,false));
    recording=Recording();recording.Exit(1,1);Require(recording.Close(S_OK));Require(recording.Pending(200,1,true));
    Require(!recording.Pending(200,1,true) && recording.failure==Failure::Replay);

    // Foreign recording uncertainty preserves the independent local state chain.
    state={};recording=Recording();
    recording.Touch(101,true,false,uint32_t(ExitKind::Clear));
    recording.Touch(102,false,true,uint32_t(ExitKind::Aliasing));
    Require(recording.foreignFailure==Failure::OtherListTouch && recording.foreignList==101 &&
        recording.foreignKind==uint32_t(ExitKind::Clear) && recording.foreignEvents==2);
    Require(!state.count);Require(state.Seed(2,depth));
    recording.Exit(1,uint32_t(ExitKind::Close));
    Require(state.Transition(all,depth,copy,none) && recording.Close(S_OK));state.Freeze();
    Require(state.failure==Failure::None && state.states[0].frozen==copy && state.states[1].frozen==copy &&
        (state.states[0].flags&FrozenKnown) && (recording.AggregateFlags()&Refused));
    Require(recording.Pending(200,1,true) && (recording.flags&PendingValidated) && !(recording.AggregateFlags()&PendingValidated));
    recording.Submitted(200);Require(recording.flags&SubmittedObserved);
    const auto savedFlags=recording.flags;recording.Reset();Require(recording.flags==savedFlags);
    Require(!recording.Close(E_FAIL) && recording.closeResult==S_OK);
    recording.Foreign(recording.list,uint32_t(ExitKind::Other));
    Require(recording.flags==savedFlags && recording.closeResult==S_OK && (recording.AggregateFlags()&Refused));

    recording=Recording();recording.Exit(1,1);recording.Touch(100,true);
    Require(!recording.Close(S_OK) && !recording.Pending(200,1,true));
    recording.Submitted(200);Require((recording.flags&SubmittedObserved) && !recording.Tracking());
    const auto failedClose=recording.closeResult;recording.Reset();Require(!recording.Close(E_FAIL) && recording.closeResult==failedClose);

    detail::ObservationWindow window;Require(!window.Event() && !window.events);
    Require(window.Begin() && window.Event() && window.events==1);window.Finish();
    Require(!window.Event() && window.events==1 && !window.Begin() && !window.open);
    Require(!window.Event() && window.events==1);window.Clear();
    Require(window.Begin() && window.Event() && window.events==1);window.Finish();

    using cvr::stereo::texture_read::detail::Allocation;
    using cvr::stereo::texture_read::detail::AliasAffects;
    const Allocation placed{10,100,100,2},disjoint{10,200,100,2},overlap{10,199,100,2},committed{0,0,0,1};
    state=Depth();state.Alias(AliasAffects(1,placed,2,disjoint,0,{}));Require(state.failure==Failure::None);
    state.Alias(AliasAffects(1,placed,2,overlap,0,{}));Require(state.failure==Failure::Aliasing);
    Require(AliasAffects(1,placed,0,{},0,{}));
    Require(!AliasAffects(1,committed,0,{},0,{}));
    Require(AliasAffects(1,committed,1,committed,0,{}));
    Require(AliasAffects(1,placed,2,{},0,{}));
    std::puts("PASS native target observed states, two planes, internal restore, split and alias refusal, close and submission generations");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
