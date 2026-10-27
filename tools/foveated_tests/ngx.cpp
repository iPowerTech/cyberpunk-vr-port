#include "Render/FoveatedNgx.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using namespace cvr::foveated::ngx;
void Check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
class FakeParameters : public Parameters {
    using Value=std::variant<unsigned,float,ID3D12Resource*>;
    std::map<std::string,Value> values;
    template<class T> Result ReadValue(const char* name,T* value) const {
        auto it=values.find(name);
        if(it==values.end())return 0xBAD0000A;
        if(auto* found=std::get_if<T>(&it->second)){*value=*found;return 1;}
        return 0xBAD00005;
    }
public:
    void Set(const char* n,unsigned v) override {values[n]=v;}
    void Set(const char* n,int v) override {values[n]=unsigned(v);}
    void Set(const char* n,float v) override {values[n]=v;}
    void Set(const char* n,ID3D12Resource* v) override {values[n]=v;}
    Result Get(const char* n,unsigned* v) const override {return ReadValue(n,v);}
    Result Get(const char* n,int* v) const override {unsigned u{};auto r=ReadValue(n,&u);*v=int(u);return r;}
    Result Get(const char* n,float* v) const override {return ReadValue(n,v);}
    Result Get(const char* n,ID3D12Resource** v) const override {return ReadValue(n,v);}
    void Set(const char*,unsigned long long) override {}
    void Set(const char*,double) override {}
    void Set(const char*,ID3D11Resource*) override {}
    void Set(const char*,void*) override {}
    Result Get(const char*,unsigned long long*) const override {return 0xBAD00005;}
    Result Get(const char*,double*) const override {return 0xBAD00005;}
    Result Get(const char*,ID3D11Resource**) const override {return 0xBAD00005;}
    Result Get(const char*,void**) const override {return 0xBAD00005;}
    void Reset() override {values.clear();}
};
int main() try {
    struct SimulatedFailure {};
    FakeParameters p,other;
    auto* resource=reinterpret_cast<ID3D12Resource*>(uintptr_t(16));
    p.Set("Width",1485u);p.Set("Preset",0x8000000Bu);p.Set("Jitter",.25f);p.Set("Color",resource);
    try {
        Override change(&p);
        change.Set("Width",891u);change.Set("Width",594u);change.Set("Preset",5u);
        change.Set("Jitter",.125f);change.Set("Color",static_cast<ID3D12Resource*>(nullptr));
        {Override inner(&p);inner.Set("Width",32u);}
        Check(Integer(&p,"Width")==594,"nested override did not restore outer value");
        throw SimulatedFailure{};
    } catch(const SimulatedFailure&) {}
    Check(Integer(&p,"Width")==1485 && Integer(&p,"Preset")==0x8000000Bu,"integer parameters were not restored");
    Check(Read<float>(&p,"Jitter")==.25f && Read<ID3D12Resource*>(&p,"Color")==resource,"resource or jitter was not restored");
    Check(Integer(&p,"Missing",77)==77,"missing parameter fallback");
    CreationRegistry registry;
    auto* original=reinterpret_cast<Handle*>(uintptr_t(32));
    auto* wrapped=reinterpret_cast<Handle*>(uintptr_t(48));
    FeatureConfig game{2560,2560,0x4B,1,{11,11,11,13,12,11}};
    registry.Remember(original,&p,1,game);
    FeatureConfig query{2560,2560,0x4B,1};
    Check(registry.Find(wrapped,&other,2,query) && query.presets==game.presets,"wrapped NGX handle lost game presets");
    auto changed=game;changed.presets.fill(5);
    registry.Remember(wrapped,&other,2,changed);
    query={2560,2560,0x4B,1};
    Check(registry.Find(original,&other,2,query) && query.presets==game.presets,"handle identity must win");
    query={2560,2560,0x4B,1};
    Check(!registry.Find(nullptr,nullptr,0,query),"ambiguous configuration must bypass instead of guessing");
    registry.Remember(wrapped,&other,2,game);
    Check(registry.Find(wrapped,&other,2,query) && query.presets==game.presets,"new game configuration was not adopted");
    query={1920,1080,0x4B,1};
    Check(!registry.Find(original,&p,1,query),"mismatched feature dimensions reused");
    std::cout<<"PASS NGX parameter restoration and game configuration inheritance\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
