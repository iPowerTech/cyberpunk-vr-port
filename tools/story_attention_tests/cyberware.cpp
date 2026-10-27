#include "Hooks/CyberwareChord.hpp"
#include <cstdlib>
#include <iostream>
using cvr::input::CyberwareHold;
int checks{};
void Check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
int main() {
    for(uint64_t interval:{7,11,16,22,33,50,100}) {
        CyberwareHold c;c.Step(false,0,false,true,1000);
        int fires=0;uint64_t first{};
        for(uint64_t now=1100;now<=3200;now+=interval) {
            auto r=c.Step(true,1,false,true,now);
            Check(r.claimed,"chord owns L3/grip while held");
            if(r.fire){++fires;first=now;}
        }
        Check(fires==1 && first>=1600 && first<1600+interval,"one fire after 0.5 s at each polling rate");
        Check(!c.Step(false,0,false,true,3250).claimed,"full release clears ownership");
    }
    CyberwareHold c;c.Step(false,0,false,true,1000);
    c.Step(true,1,false,true,1100);c.Step(true,.65f,false,true,1200);c.Step(true,.5f,false,true,1300);
    c.Step(true,.6f,false,true,1400);c.Step(true,.5f,false,true,1599);
    Check(c.Step(true,.55f,false,true,1600).fire,"grip hysteresis rides threshold noise");
    Check(!c.Step(true,0,false,true,1700).fire,"partial release does not repeat");
    for(int i=0;i<10;++i)Check(!c.Step(true,1,false,true,1750+i*100).fire,"both must release to rearm");
    c.Step(false,0,false,true,2800);
    c.Step(true,1,false,true,2900);c.Step(true,1,false,true,3100);
    Check(!c.Step(true,1,false,true,3600).fire,"tracking gap cannot complete hold");
    c.Step(false,0,false,true,3700);c.Step(true,1,false,true,3800);
    Check(!c.Step(true,1,true,true,3900).fire,"dual-stick overlay priority");
    for(int i=0;i<8;++i)Check(!c.Step(true,1,false,true,4000+i*100).fire,"removing R3 cannot fire a cancelled hold");
    c.Step(false,0,false,true,5000);c.Step(true,1,false,true,5100);
    c.Step(true,1,false,false,5200);
    for(int i=0;i<8;++i)Check(!c.Step(true,1,false,true,5300+i*100).fire,"menu resume held cannot fire");
    c.Step(false,0,false,true,6200);c.Step(false,1,false,true,6300);
    Check(!c.Step(false,1,false,true,6400).claimed,"grip alone unchanged");
    c.Step(false,0,false,true,6500);
    Check(!c.Step(true,0,false,true,6600).claimed,"L3 alone unchanged");
    c.Step(false,0,false,true,6700);
    c.Step(true,1,false,true,6800);c.Step(true,1,false,true,6999);
    Check(!c.Step(false,0,false,true,7000).fire,"short hold cancels");
    c.Step(true,1,false,true,7100);c.Step(true,1,false,true,7300);c.Step(true,1,false,true,7499);
    Check(!c.Step(true,1,false,true,7599).fire,"499 ms does not fire");
    Check(c.Step(true,1,false,true,7600).fire,"500 ms fires");
    std::cout<<"PASS "<<checks<<" cyberware hold, jitter, cancellation and polling-rate checks\n";
}
