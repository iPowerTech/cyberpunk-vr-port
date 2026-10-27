#include "Render/MaterialReadbackState.hpp"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

using cvr::stereo::material_read::ReadbackState;
using Stage=ReadbackState::Stage;
using Permit=ReadbackState::PermitUpdate;
namespace {
void Require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void InitialAndInvalidation(){
    ReadbackState state;
    Require(state.State()==Stage::Recorded && !state.PermitEnabled(),"Initial permit must be disabled");
    Require(!state.CanSeal(true) && !state.Seal(true),"Unsubmitted recording sealed");
    Require(state.BeforeSubmit(0,false,false)==Permit::None && state.State()==Stage::Recorded,
        "Unrelated submission changed capture");
    state.Submitted();Require(!state.PendingSubmissions(),"Unrelated completion changed pending count");
    state.Invalidated();
    Require(state.BeforeSubmit(1,true,true)==Permit::Disable,"Invalidated capture was enabled");
    state.Submitted();
    Require(state.State()==Stage::Rejected && !state.PermitEnabled() && !state.CanSeal(true),
        "Initially invalid capture became publishable");

    for(const bool complete:{false,true}){
        ReadbackState late;
        Require(late.BeforeSubmit(1,true,true)==Permit::Enable,"Valid capture not enabled");
        if(complete)late.Submitted();
        late.Invalidated();
        Require(late.State()==Stage::Rejected && late.PermitEnabled(),"Late invalidation changed frozen permit");
        Require(!late.CanSeal(true) && !late.Seal(true),"Late invalidation sealed rejected capture");
        Require(late.PermitMayBeInFlight(false),"Rejection incorrectly certified GPU completion");
        if(!complete){
            Require(late.PermitMayBeInFlight(true),"Pending callback allowed permit mutation");
            late.Submitted();
        }
        Require(!late.PermitMayBeInFlight(true),"Completed original execution remained in flight");
    }
}
void DuplicateAndReplay(){
    ReadbackState duplicate;
    Require(duplicate.BeforeSubmit(2,true,true)==Permit::Disable,"Duplicate batch enabled capture");
    Require(duplicate.PendingSubmissions()==1 && duplicate.State()==Stage::Rejected,
        "Duplicate batch must count one callback");
    duplicate.Submitted();
    Require(!duplicate.CanSeal(true) && !duplicate.PermitMayBeInFlight(false),"Disabled duplicate became readable");

    for(const bool complete:{false,true}){
        ReadbackState replay;
        Require(replay.BeforeSubmit(1,true,true)==Permit::Enable,"First submission rejected");
        if(complete)replay.Submitted();
        Require(replay.BeforeSubmit(1,true,true)==Permit::None,"Replay changed frozen permit");
        Require(replay.State()==Stage::Rejected && replay.PermitEnabled(),"Replay was not rejected immutably");
        Require(replay.PendingSubmissions()==(complete?1u:2u),"Replay lost pending callbacks");
        replay.Submitted();if(!complete)replay.Submitted();
        Require(!replay.PendingSubmissions() && !replay.CanSeal(true),"Replay became publishable");
        Require(replay.PermitMayBeInFlight(true),"Original fence incorrectly certified replay completion");
    }
}
void ResetAndMetadata(){
    ReadbackState reset;
    reset.ResetBeforeSubmit();
    Require(reset.State()==Stage::Rejected && reset.BeforeSubmit(1,true,true)==Permit::Disable,
        "Reset before submission enabled stale capture");
    reset.Submitted();Require(!reset.CanSeal(true),"Reset recording sealed");

    ReadbackState missing;
    Require(missing.BeforeSubmit(1,true,false)==Permit::Disable && missing.State()==Stage::Rejected,
        "Lost recording ownership was accepted");
    Require(!missing.PendingSubmissions() && !missing.PermitEnabled(),"Unowned recording acquired GPU work");
    Require(missing.BeforeSubmit(1,true,true)==Permit::None,"Lost recording was rearmed");
    missing.Submitted();Require(!missing.CanSeal(true),"Lost recording ownership recovered unexpectedly");

    ReadbackState metadata;
    Require(metadata.BeforeSubmit(1,false,true)==Permit::Disable,"Invalid metadata enabled capture");
    metadata.Submitted();
    Require(metadata.BeforeSubmit(1,true,true)==Permit::None && !metadata.PermitEnabled(),
        "Later valid metadata rearmed rejected capture");
    metadata.Submitted();Require(!metadata.CanSeal(true),"Rejected metadata became publishable");

    ReadbackState afterExecute;
    Require(afterExecute.BeforeSubmit(1,true,true)==Permit::Enable,"Valid capture not enabled");
    afterExecute.ResetBeforeSubmit();
    Require(afterExecute.State()==Stage::Submitting,"Reset after Execute rejected pending callback");
    Require(afterExecute.BeforeSubmit(1,true,false)==Permit::None && afterExecute.PendingSubmissions()==1,
        "Reused command list was mistaken for replay");
    afterExecute.Submitted();afterExecute.ResetBeforeSubmit();
    Require(afterExecute.CanSeal(true),"Reset after submission prevented completion");
}
void SealAndReplay(){
    ReadbackState state;
    Require(state.BeforeSubmit(1,true,true)==Permit::Enable,"Valid capture not enabled");
    Require(!state.CanSeal(true) && !state.Seal(true) && state.PermitMayBeInFlight(true),
        "Submitting capture sealed before callback");
    state.Submitted();
    Require(state.State()==Stage::Submitted && !state.CanSeal(false) && !state.Seal(false),
        "Unfinished execution sealed");
    Require(state.CanSeal(true) && !state.PermitMayBeInFlight(true) && state.Seal(true),
        "Completed capture could not seal");
    Require(state.State()==Stage::Sealed && !state.PermitEnabled() && !state.Seal(true),
        "Seal did not permanently disable permit exactly once");
    Require(state.BeforeSubmit(1,true,true)==Permit::None && state.State()==Stage::Rejected,
        "Replay after seal was accepted");
    Require(!state.PermitEnabled() && !state.PermitMayBeInFlight(false),"Replay rearmed sealed permit");
    state.Submitted();
    Require(!state.CanSeal(true) && !state.Seal(true),"Replay produced another publishable capture");
}
}
int main()try{
    InitialAndInvalidation();DuplicateAndReplay();ResetAndMetadata();SealAndReplay();
    ReadbackState disabledReplay;disabledReplay.BeforeSubmit(1,false,true);disabledReplay.Submitted();
    Require(!disabledReplay.ReplayObserved(),"Initial rejection is not replay");
    disabledReplay.BeforeSubmit(1,false,true);disabledReplay.Submitted();
    Require(disabledReplay.ReplayObserved() && !disabledReplay.PermitEnabled(),"Disabled replay was invisible");
    ReadbackState duplicate;duplicate.BeforeSubmit(2,true,true);
    Require(duplicate.ReplayObserved(),"Duplicate initial list was not replay");
    for(const bool replay:{false,true}){
        ReadbackState state;Require(state.BeforeSubmit(1,true,true)==Permit::Enable,"Capture not armed");
        state.Invalidated();Require(!state.DisarmRejected(true),"Pending copy disarmed");state.Submitted();
        Require(!state.DisarmRejected(false),"Unfinished copy disarmed");
        if(replay){state.BeforeSubmit(1,true,true);state.Submitted();}
        Require(state.DisarmRejected(true)==!replay,"Rejected copy disarm ignored replay");
        Require(state.State()==Stage::Rejected && state.PermitEnabled()==replay,"Rejected payload became publishable");
        Require(!state.DisarmRejected(true),"Rejected copy disarmed twice");
    }
    std::puts("PASS material readback permits, invalidation, reset, replay rejection and completion sealing");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
