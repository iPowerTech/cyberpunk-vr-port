#pragma once
#include <cstdint>

namespace cvr::quest {
enum class Attention : uint32_t { Native, Pacifica, Workspot };
// Adapted from Ajson44's final V39 rules (a7ddea59 / 28a73447).
// Walking authored scenes must keep native attention: the broader Tier2 rule
// blocked Judy's apartment doorway. Passenger choices keep their native ray.
inline Attention SelectAttention(int started,int done,int locomotion,int tier,
                                  bool workspot,bool mounted,int vehicle,bool choices) {
    if(started>0 && done<=0)return Attention::Pacifica;
    if(!mounted && tier>=2 && locomotion!=9)return Attention::Native;
    if(mounted && choices && (vehicle==3 || vehicle==4 || vehicle==7))return Attention::Native;
    return locomotion==9 && (tier>=2 || workspot) ? Attention::Workspot:Attention::Native;
}
struct ObjectKey {
    uintptr_t address{},reference{};
    bool operator==(const ObjectKey&) const = default;
    explicit operator bool() const {return address && reference;}
};
inline bool Fresh(uint64_t stamp,uint64_t now,uint64_t lifetime) {
    return stamp && now>=stamp && now-stamp<lifetime;
}
struct AttentionContext {
    ObjectKey player{};
    uint64_t entity{},stamp{};
    Attention mode{};
    bool Matches(ObjectKey owner,uint64_t ownerEntity,uintptr_t currentPlayer,uint64_t now) const {
        return mode!=Attention::Native && player && owner==player && ownerEntity==entity &&
               currentPlayer==player.address && Fresh(stamp,now,750);
    }
};

// Capture the physical X edge before the normal scanner/interaction remaps.
// Keep the press tied to one live clue; never replay it on a new target. Once
// claimed, the whole hold is swallowed even if the target/menu changes.
class ManualClueInput {
    ObjectKey armed{},pending{};
    uint64_t stamp{},pressStamp{};
    bool seen{},down{},consumed{};
public:
    void Suspend() {armed={};pending={};stamp=0;seen=false;}
    void Arm(ObjectKey key,uint64_t now) {
        if(key!=armed)pending={};
        armed=key;stamp=key ? now:0;
    }
    bool Button(bool pressed,bool allowed,uint64_t now) {
        const bool edge=seen && pressed && !down;
        seen=true;down=pressed;
        if(!pressed)consumed=false;
        if(!allowed || !Fresh(stamp,now,250)){armed={};pending={};stamp=0;}
        if(edge && allowed && armed) {pending=armed;pressStamp=now;consumed=true;}
        return pressed && consumed;
    }
    bool Take(ObjectKey key,uint64_t now) {
        const bool ready=key && key==armed && key==pending && Fresh(stamp,now,250) && Fresh(pressStamp,now,250);
        pending={};return ready;
    }
};
// Script-thread publication; native targeting/input only use identities/values.
void PublishAttention(AttentionContext context);
bool UpdateManualClue(ObjectKey player,ObjectKey clue);
bool ConsumeManualClueButton(bool pressed,bool allowed);
void SuspendManualClueInput();
}
