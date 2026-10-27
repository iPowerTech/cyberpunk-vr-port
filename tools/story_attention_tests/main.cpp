#include "Quest/StoryAttention.hpp"
#include <cstdlib>
#include <iostream>
using namespace cvr::quest;
int checks{};
void Check(bool ok,const char* name) {++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
int main() {
    Check(SelectAttention(0,0,0,1,false,false,0,false)==Attention::Native,"ordinary gameplay");
    Check(SelectAttention(1,0,0,1,false,false,0,false)==Attention::Pacifica,"active Pacifica stage");
    Check(SelectAttention(1,1,0,1,false,false,0,false)==Attention::Native,"completed stage");
    Check(SelectAttention(0,0,0,3,true,false,0,false)==Attention::Native,"Judy doorway keeps native attention");
    Check(SelectAttention(0,0,9,3,false,false,0,false)==Attention::Workspot,"Judy pre-BD locomotion 9");
    Check(SelectAttention(0,0,9,1,true,false,0,false)==Attention::Workspot,"explicit workspot");
    Check(SelectAttention(0,0,9,1,false,false,0,false)==Attention::Native,"locomotion alone insufficient");
    Check(SelectAttention(0,0,0,1,true,false,0,false)==Attention::Native,"workspot alone insufficient");
    Check(SelectAttention(1,0,0,3,false,false,0,false)==Attention::Pacifica,"stage precedes authored-scene exception");
    for(int vehicle:{3,4,7}) {
        Check(SelectAttention(0,0,9,3,true,true,vehicle,true)==Attention::Native,"passenger choices");
        Check(SelectAttention(0,0,9,3,true,true,vehicle,false)==Attention::Workspot,"passenger workspot without choices");
    }
    for(int tier:{2,3,4,5}) for(int loco:{0,1,6,8,10})
        Check(SelectAttention(0,0,loco,tier,true,false,0,false)==Attention::Native,"no broad authored-scene override");
    ObjectKey player{0x1000,0x2000},a{0x3000,0x4000},b{0x5000,0x6000};
    AttentionContext context{player,1,1000,Attention::Workspot};
    Check(context.Matches(player,1,player.address,1001),"same player and fresh context");
    Check(!context.Matches(a,1,player.address,1001),"nearby NPC excluded");
    Check(!context.Matches({player.address,0x2222},1,player.address,1001),"recycled address excluded");
    Check(!context.Matches(player,2,player.address,1001),"wrong entity excluded");
    Check(!context.Matches(player,1,a.address,1001),"player replacement excluded");
    Check(!context.Matches(player,1,player.address,1750),"stopped script lease expires");
    Check(!context.Matches(player,1,player.address,999),"clock reversal rejected");
    context.mode=Attention::Native;
    Check(!context.Matches(player,1,player.address,1001),"explicit disable wins");

    ManualClueInput input;
    input.Arm(a,1000);
    Check(!input.Button(true,true,1001)&&!input.Take(a,1002),"held X on first observation cannot inspect");
    Check(!input.Button(false,true,1003),"release rearms");
    Check(input.Button(true,true,1004),"explicit X claimed");
    input.Arm(a,1005);
    Check(input.Take(a,1005)&&!input.Take(a,1006),"one request exactly once");
    input.Arm({},1007);
    Check(input.Button(true,true,1008),"newly revealed Take stays suppressed");
    Check(input.Button(true,false,1009),"consumed hold stays suppressed through menu");
    Check(!input.Button(false,false,1010),"release clears suppression");
    Check(!input.Button(true,true,1011),"next ordinary X passes");
    input.Button(false,true,1100);input.Arm(a,1100);
    input.Button(true,true,1101);input.Arm(b,1102);
    Check(!input.Take(b,1103)&&!input.Take(a,1103),"target switch drops request");
    Check(input.Button(true,true,1104)&&!input.Take(b,1105),"held press cannot retarget");
    input.Button(false,true,1200);input.Arm(a,1200);
    Check(!input.Button(true,true,1450)&&!input.Take(a,1451),"stale clue cannot inspect");
    input.Button(false,true,1500);input.Arm(a,1500);input.Button(true,true,1501);
    input.Arm(a,1800);
    Check(!input.Take(a,1801),"delayed request expires even after refresh");
    input.Button(false,true,1900);input.Arm(a,1900);input.Suspend();input.Arm(a,1901);
    Check(!input.Button(true,true,1902)&&!input.Take(a,1903),"tracking resume while held cannot inspect");
    input.Button(false,true,2000);input.Arm(a,2000);
    Check(!input.Button(true,false,2001)&&!input.Take(a,2002),"paused input cannot inspect");
    std::cout<<"PASS "<<checks<<" story policy, owner lifetime and explicit-input checks\n";
}
