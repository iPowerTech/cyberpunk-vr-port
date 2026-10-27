#include "Runtimes/PipelinePrediction.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

void Check(bool value,const char* text){if(!value){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
int main(){
    using cvr::tracking::PipelinePrediction;
    for(int hz:{60,72,90,120,144}){
        const int64_t period=1000000000/hz;int64_t display=1000000000,previousLead=0;
        PipelinePrediction p;
        for(int i=0;i<240;++i){
            display+=period;const auto target=p.Target(display,period,display+period*3,true,1);
            const auto lead=target-display;
            Check(lead>=0 && lead<=std::min<int64_t>(2*period,25000000),"prediction bound");
            Check(std::abs(lead-previousLead)<=period*.101+2,"prediction slew");previousLead=lead;
        }
        Check(previousLead>=std::min<int64_t>(2*period,25000000)-10,"stable fit converges");
        for(int i=0;i<240;++i){display+=period;p.Target(display,period,0,false,1);}
        display+=period;Check(p.Target(display,period,0,false,1)-display<10,"stale fit decays");
        p.Reset();display+=period;Check(p.Target(display,period,display+period,true,0)==display,"A/B off is exact");
        display+=period;Check(p.Target(display,period,display+period,true,std::numeric_limits<float>::quiet_NaN())==display,"invalid strength");
        p.Reset();for(int i=0;i<240;++i){display+=period;p.Target(display,period,display+period,true,1);}
        display+=1000000000;Check(p.Target(display,period,0,false,1)==display,"resume clears old horizon");
        Check(p.Target(display-1000000000,period,0,false,1)==display-1000000000,"time reversal clears horizon");
        p.Reset();display+=period;Check(p.Target(display,period,display+period*10,true,1)==display,"reject implausible future");
        display+=period;Check(p.Target(display,period,display-period*2,true,1)==display,"reject stale target");
    }
    std::puts("PASS: bounded prediction at60/72/90/120/144Hz, convergence, slew, stale fits, resets and A/B off");
}
