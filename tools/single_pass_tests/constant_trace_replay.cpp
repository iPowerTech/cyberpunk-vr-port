#include "Render/ConstantTraceReplay.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2 && argc!=4)throw std::runtime_error("Usage: constant_trace_replay trace.bin [read-token address]");
    std::ifstream stream(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("Cannot open trace");const auto bytes=stream.tellg();
    using namespace cvr::stereo::constants;
    if(bytes<=0 || bytes%sizeof(TraceEvent) || bytes>TraceCapacity*sizeof(TraceEvent))throw std::runtime_error("Trace size invalid");
    std::vector<TraceEvent> events(size_t(bytes)/sizeof(TraceEvent));stream.seekg(0);stream.read(reinterpret_cast<char*>(events.data()),bytes);
    if(!stream)throw std::runtime_error("Trace truncated");const auto result=ReplayConstantTrace(events,
        argc==4?std::wcstoull(argv[2],nullptr,0):0,argc==4?std::wcstoull(argv[3],nullptr,0):0);
    std::printf("{\"events\":%zu,\"queries\":%llu,\"mismatches\":%llu,\"statuses\":[",events.size(),result.queries,result.mismatches);
    for(size_t i=0;i<result.statuses.size();++i)std::printf("%s%llu",i?",":"",result.statuses[i]);
    std::printf("],\"read\":[%llu,%llu,%u,%u],\"unorderedWrites\":[",result.read.queue,result.read.batch,result.read.index,result.read.node);
    bool comma=false;for(const auto& w:result.related)if(w.relation==3) {
        std::printf("%s{\"id\":%u,\"queue\":%llu,\"batch\":%llu,\"node\":%u,\"recording\":%u,\"command\":%llu,\"payload\":%llu}",
            comma?",":"",w.id,w.context.queue,w.context.batch,w.context.node,w.point.recording,w.point.command,w.payload);comma=true;
    }
    std::puts("]}");return result.mismatches?2:0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
