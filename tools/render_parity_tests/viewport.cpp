#include "Stereo/VrcamViewport.hpp"
#include <iostream>
#include <stdexcept>

using namespace cvr::detail;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()try {
    constexpr uintptr_t view=0x2E9AA882980,source=view+PendingVrcamViewport::InputOffset;
    constexpr auto caller=PendingVrcamViewport::CommitCallerRva;
    const VrcamRenderRect destination{0,0,2560,2560},sentinel{3,5,7,9};
    VrcamRenderRect rect=sentinel;
    PendingVrcamViewport pending;
    Check(!pending.Apply(source,caller,destination,rect) && rect==sentinel,"unarmed or MAIN viewport changed");
    pending={source,0,0};
    Check(!pending.Apply(source,0x4E3E82,destination,rect) && pending.input==source,"dimension helper consumed pending commit");
    Check(!pending.Apply(source+0x1000,caller,destination,rect) && pending.input==source,"another view consumed the override");
    Check(pending.Apply(source,caller,destination,rect) && rect==destination && !rect.Empty(),"cold VRCAM retained an empty rect");
    Check(!pending.Apply(source,caller,destination,rect),"override applied twice");

    // In the dump, native AS preparation sees a one-entry view list and all
    // four rect words are zero. Reproduce that selection and its first-frame
    // repair without skipping AS work or altering any ray-tracing feature bits.
    struct View {VrcamRenderRect rect;int32_t position[3];};
    View recorded{{},{5596,474559,208675}};
    const auto select=[](const View& entry){return entry.rect.Empty()?nullptr:&entry;};
    Check(select(recorded)==nullptr,"crash fixture does not reproduce null selection");
    pending={source,0,0};
    Check(pending.Apply(source,caller,destination,recorded.rect),"cold commit failed");
    Check(select(recorded)==&recorded && select(recorded)->position[2]==208675,"native RT camera selection still fails");

    for(const auto dims:{std::pair{1485u,1485u},std::pair{1832u,2032u},std::pair{4096u,4280u}}) {
        pending={source,dims.first,dims.second};rect={};
        Check(pending.Apply(source,caller,destination,rect),"initialized viewport rejected");
        Check(rect==VrcamRenderRect{0,0,static_cast<int32_t>(dims.first),static_cast<int32_t>(dims.second)},"existing render-size behavior changed");
    }
    for(auto bounds:{VrcamRenderRect{},VrcamRenderRect{10,0,10,100},VrcamRenderRect{0,8,10,7}}) {
        pending={source,0,0};rect=sentinel;
        Check(!pending.Apply(source,caller,bounds,rect) && rect==sentinel && !pending.input,"invalid bounds installed or stale pending state retained");
    }
    pending={source,0,1485};rect={};
    Check(pending.Apply(source,caller,destination,rect) && rect==destination,"partial size did not use coherent destination bounds");
    pending={source,0xffffffffu,1485};rect=sentinel;
    Check(!pending.Apply(source,caller,destination,rect) && rect==sentinel,"invalid dimensions wrapped into a signed rectangle");
    std::cout<<"PASS recorded RT null-view selection, cold viewport commit, caller/view isolation, one-shot and initialized dimensions\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
