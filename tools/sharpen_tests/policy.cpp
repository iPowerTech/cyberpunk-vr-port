#include "Render/SharpenPolicy.hpp"
#include "Core/LiveControls.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try {
    using namespace cvr::sharpen;
    LiveControls controls{};
    Check(DefaultAmount==0 && controls.xrSharpmix==0,"CAS must default off");
    UpscalerGate gate;
    Check(!gate.BlocksCas() && gate.Amount(.75f)==.75f,"native rendering must preserve requested strength");
    gate.SelectDlss(true);
    Check(gate.BlocksCas() && gate.Amount(1)==0,"unknown DLSS mode must wait for game quality");
    gate.ObserveGameQuality(1,5);
    Check(gate.BlocksCas() && gate.Amount(1)==0,"DLAA must bypass CAS");
    gate.ObserveGameQuality(2,1);
    Check(gate.BlocksCas(),"second eye overrode MAIN's DLAA selection");
    gate.SelectDlss(true);
    Check(gate.BlocksCas(),"rebuilding DLSS graph lost its DLAA mode");
    gate.ObserveGameQuality(1,1);
    Check(!gate.BlocksCas() && gate.Amount(.75f)==.75f,"leaving DLAA did not restore the user's requested strength");
    gate.ObserveGameQuality(2,5);
    Check(!gate.BlocksCas(),"non-MAIN DLAA was mistaken for the game's selected mode");
    gate.ObserveGameQuality(1,5);gate.SelectDlss(false);
    Check(!gate.BlocksCas() && gate.Amount(.75f)==.75f,"native/FSR/XeSS selection retained a stale DLAA block");
    gate.SelectDlss(true);
    Check(gate.BlocksCas(),"re-entering DLSS reused an obsolete quality observation");
    gate.ObserveGameQuality(1,~0u);
    Check(gate.BlocksCas(),"missing NGX quality enabled CAS");
    for(unsigned quality=0;quality<5;++quality) {
        gate.ObserveGameQuality(1,quality);Check(!gate.BlocksCas(),"upscaling quality disabled CAS");
    }
    Check(gate.Amount(std::numeric_limits<float>::quiet_NaN())==0 && gate.Amount(-1)==0 && gate.Amount(2)==1,"invalid amount handling");
    std::cout<<"PASS CAS default 0, DLAA suppression, upscaler transitions and saved strength\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
