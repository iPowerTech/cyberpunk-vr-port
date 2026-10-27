#include "Render/NativePacketKey.hpp"
#include "Render/NativePacketPass.hpp"
#include "Render/StereoMeshPlan.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <chrono>
using namespace cvr::stereo;
using namespace cvr::stereo::packets;
static void Require(bool value){if(!value)throw std::runtime_error("Invalid packet fixture");}
template<class T> std::vector<T> Read(const std::filesystem::path& name) {
    std::ifstream f(name,std::ios::binary|std::ios::ate);Require(bool(f));const auto bytes=f.tellg();
    Require(bytes>=0 && bytes<=64*1024*1024 && size_t(bytes)%sizeof(T)==0);
    std::vector<T> data(size_t(bytes)/sizeof(T));f.seekg(0);if(bytes)f.read(reinterpret_cast<char*>(data.data()),bytes);Require(bool(f));return data;
}
template<class T> T At(const auto& bytes,size_t offset){T value{};std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;}
int wmain(int argc,wchar_t** argv) try {
    Require(argc==2 || argc==3);const std::wstring stem=argv[1];
    std::ofstream jsonFile;if(argc==3){jsonFile.open(std::filesystem::path(argv[2]),std::ios::binary);Require(bool(jsonFile));}
    std::string json;
    const auto records=Read<Record>(stem+L".records.bin");const auto packets=Read<Packet>(stem+L".packets.bin");
    const auto inputs=Read<Inputs>(stem+L".inputs.bin");const auto blob=Read<uint8_t>(stem+L".input-blob.bin");
    Require(inputs.size()==packets.size());
    using Key=std::tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,std::array<uint8_t,40>>;
    struct Group {std::array<std::vector<Record>,2> chunks;std::array<std::vector<uint64_t>,2> keys;
        std::array<std::vector<uint32_t>,2> packetIds;std::map<std::string,uint64_t> ids;uint32_t unavailable{};};
    std::map<Key,Group> groups;
    std::vector<bool> seen(packets.size());
    for(const auto& row:records) {
        Require(row.side<=1 && (row.status==0 || row.status==1) && (row.metadataFlags&4) && row.endQpc>=row.beginQpc);
        Require(row.offset<=packets.size() && row.count<=packets.size()-row.offset);
        const auto plane=At<uint32_t>(row.groupBytes,16),flags=At<uint32_t>(row.groupBytes,8);
        const auto modes=uint32_t(row.argumentBytes[24])|(uint32_t(row.argumentBytes[25])<<8)|(uint32_t(row.argumentBytes[26])<<16);
        groups[{row.frame,row.node,plane,row.groupBytes[20],flags,modes,PassIdentity(row)}].chunks[row.side==1?0:1].push_back(row);
    }
    for(auto& [groupKey,group]:groups)for(unsigned eye=0;eye<2;++eye) {
        std::vector<PacketRange> ranges;Require(AssemblePass(group.chunks[eye],packets.size(),ranges));
        for(auto range:ranges)for(uint32_t i=range.offset;i<range.offset+range.count;++i) {
            Require(!seen[i]);seen[i]=true;std::string key;group.packetIds[eye].push_back(i);
            if(!MeshCandidateKey(packets[i],inputs[i],blob,key)){group.keys[eye].push_back(0);++group.unavailable;continue;}
            const auto [it,added]=group.ids.emplace(std::move(key),group.ids.size()+1);group.keys[eye].push_back(it->second);
        }
    }
    Require(std::all_of(seen.begin(),seen.end(),[](bool v){return v;}));
    uint32_t total{},common{},unavailable{};bool comma=false;double planningUs{};
    std::string pairs;
    json="{\"renderingEnabled\":false,\"groups\":[\n";
    for(const auto& [key,group]:groups) {
        MeshPlan plan;const auto begin=std::chrono::steady_clock::now();
        Require(BuildStereoMeshPlan(group.keys[0],group.keys[1],plan));
        planningUs+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
        const auto frame=std::get<0>(key),plane=std::get<2>(key);
        uint32_t masks[3]{};for(const auto& draw:plan.draws)++masks[draw.mask-1];
        for(const auto& draw:plan.draws)if(draw.mask==3){char pair[80]{};
            std::snprintf(pair,sizeof(pair),"%s[%u,%u]",pairs.empty()?"":",",group.packetIds[0][draw.source[0]],group.packetIds[1][draw.source[1]]);pairs+=pair;}
        char line[512]{};std::snprintf(line,sizeof(line),"%s{\"frame\":%u,\"plane\":%u,\"partitions\":[%zu,%zu],\"sourcePackets\":[%zu,%zu],\"plannedPackets\":%zu,\"masks\":[%u,%u,%u],\"ineligible\":%u}",
            comma?",\n":"",frame,plane,group.chunks[0].size(),group.chunks[1].size(),group.keys[0].size(),group.keys[1].size(),plan.draws.size(),masks[0],masks[1],masks[2],group.unavailable);
        json+=line;
        comma=true;total+=uint32_t(group.keys[0].size()+group.keys[1].size());common+=plan.common;unavailable+=group.unavailable;
    }
    char tail[512]{};std::snprintf(tail,sizeof(tail),"\n],\"nativePackets\":%u,\"candidatePlannedPackets\":%u,\"commonCandidates\":%u,\"ineligible\":%u,\"planningOnlyUs\":%.3f",total,total-common,common,unavailable,planningUs);
    json+=tail;std::fwrite(json.data(),1,json.size(),stdout);std::puts("}");
    if(argc==3){jsonFile<<json<<",\"commonPacketPairs\":["<<pairs<<"]}\n";Require(bool(jsonFile));}
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
