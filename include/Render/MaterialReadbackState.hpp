#pragma once
#include <cstdint>
#include <limits>

namespace cvr::stereo::material_read {
class ReadbackState {
public:
    enum class Stage {Recorded,Submitting,Submitted,Sealed,Rejected};
    enum class PermitUpdate {None,Enable,Disable};

    // The caller initializes the permit to zero and serializes every operation.
    PermitUpdate BeforeSubmit(uint32_t occurrences,bool valid,bool currentRecordingOwned) {
        if(!occurrences)return PermitUpdate::None;
        if(!currentRecordingOwned){
            // A reset after submission started belongs to a different recording.
            if(frozen_)return PermitUpdate::None;
            frozen_=true;stage_=Stage::Rejected;return PermitUpdate::Disable;
        }
        if(pending_!=std::numeric_limits<uint32_t>::max())++pending_;
        else replayed_=true;
        if(frozen_){stage_=Stage::Rejected;replayed_=true;return PermitUpdate::None;}
        frozen_=true;
        if(occurrences!=1)replayed_=true;
        if(stage_!=Stage::Recorded || occurrences!=1 || !valid){
            stage_=Stage::Rejected;return PermitUpdate::Disable;
        }
        enabled_=true;stage_=Stage::Submitting;return PermitUpdate::Enable;
    }
    void Submitted() {
        if(!pending_)return;
        --pending_;submitted_=true;
        if(stage_==Stage::Submitting && !pending_)stage_=Stage::Submitted;
    }
    void ResetBeforeSubmit() {
        // Reset may follow Execute before its Submitted callback arrives.
        if(!frozen_)stage_=Stage::Rejected;
    }
    void Invalidated() {stage_=Stage::Rejected;}
    bool CanSeal(bool fencesComplete) const {
        return fencesComplete && stage_==Stage::Submitted && submitted_ && !pending_ &&
            enabled_ && !sealed_ && !replayed_;
    }
    // True authorizes the caller's permanent zero write before mapping readback.
    bool Seal(bool fencesComplete) {
        if(!CanSeal(fencesComplete))return false;
        enabled_=false;sealed_=true;stage_=Stage::Sealed;return true;
    }
    bool DisarmRejected(bool fencesComplete) {
        if(stage_!=Stage::Rejected || !fencesComplete || !submitted_ || pending_ || replayed_ || !enabled_)return false;
        enabled_=false;sealed_=true;return true;
    }
    Stage State() const {return stage_;}
    bool ReplayObserved() const {return replayed_;}
    uint32_t PendingSubmissions() const {return pending_;}
    bool PermitEnabled() const {return enabled_;}
    bool PermitMayBeInFlight(bool fencesComplete) const {
        // The original completion fences cannot certify a replay's completion.
        return enabled_ && (pending_ || replayed_ || (submitted_ && !fencesComplete));
    }
private:
    Stage stage_=Stage::Recorded;
    uint32_t pending_{};
    bool frozen_{},submitted_{},enabled_{},sealed_{},replayed_{};
};
}
