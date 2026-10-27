#include "Render/StereoInstanceUnion.hpp"
#include <algorithm>
#include <cstdio>
#include <random>
#include <stdexcept>
using namespace cvr::stereo;
static void Require(bool value){if(!value)throw std::runtime_error("Instance union assertion failed");}
using Record=std::array<uint8_t,64>;
static std::vector<Record> Decode(const std::vector<uint8_t>& data) {
    Require(data.size()%64==0);std::vector<Record> result(data.size()/64);
    if(!data.empty())std::memcpy(result.data(),data.data(),data.size());return result;
}
static std::span<const uint8_t> Bytes(const std::vector<Record>& records) {
    return {reinterpret_cast<const uint8_t*>(records.data()),records.size()*64};
}
static void RoundTrip(const std::vector<Record>& a,const std::vector<Record>& b) {
    StereoInstanceGroups result;Require(MergeStereoInstances({Bytes(a),64},{Bytes(b),64},{true,true,false},result));
    const auto common=Decode(result.records[2]);
    for(unsigned eye=0;eye<2;++eye) {
        auto actual=Decode(result.records[eye]);actual.insert(actual.end(),common.begin(),common.end());
        auto expected=eye?b:a;std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());
        Require(actual==expected);
    }
}
int main() try {
    // Actual paired transform+skinning record from capture events 10042/33519.
    const char* hex="4320a83c2f72e13eb47062be6c4e541b2da9dcbe4890fc3df57e523e66df07fda891743efe203d3ecba5c73eb14b090100000000000000000000000001000000";
    Record captured{};for(unsigned i=0;i<64;++i){unsigned value{};std::sscanf(hex+i*2,"%2x",&value);captured[i]=uint8_t(value);}
    std::vector<Record> first{captured},second{captured};StereoInstanceGroups result;
    Require(MergeStereoInstances({Bytes(first),64},{Bytes(second),64},{true,true,false},result));
    Require(result.Count(3)==1 && result.Count(1)==0 && result.Count(2)==0);
    // Same transform with a different skinning address must stay per-eye.
    second[0][48]^=1;
    Require(MergeStereoInstances({Bytes(first),64},{Bytes(second),64},{true,true,false},result));
    Require(result.Count(3)==0 && result.Count(1)==1 && result.Count(2)==1);
    const auto unchanged=result.records;
    Require(!MergeStereoInstances({Bytes(first),64},{Bytes(second),64},{true,false,false},result));
    Require(!MergeStereoInstances({Bytes(first),64},{Bytes(second),64},{true,true,true},result));
    Require(!MergeStereoInstances({Bytes(first),64},{Bytes(second),48},{true,true,false},result));Require(result.records==unchanged);
    RoundTrip({},{});RoundTrip(first,{});RoundTrip({},second);RoundTrip({captured,captured},{captured});
    std::mt19937 random(0x53544552);std::array<Record,32> pool{};
    for(auto& record:pool)for(auto& byte:record)byte=uint8_t(random());
    for(unsigned test=0;test<1000;++test) {
        first.resize(random()%128);second.resize(random()%128);
        for(auto& item:first)item=pool[random()%pool.size()];for(auto& item:second)item=pool[random()%pool.size()];
        RoundTrip(first,second);
    }
    std::puts("PASS captured instances, distinct skinning, duplicates, one-eye objects, unsupported ordering/InstanceID and 1000 visibility unions");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
