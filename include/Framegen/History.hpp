#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cvr::framegen {
inline constexpr size_t GraphPoints=120;
struct HistorySummary {
    uint32_t samples{};
    double average{},minimum{},peak{},p99{},p999{},low1{},low01{};
};
// Bounded CPU-only history. Percentile sorting runs on overlay refresh, not in
// the producer hooks; empty/undersampled lows remain unavailable (zero).
template<size_t Capacity=16384> class FrameHistory {
    struct Sample {double stamp{},ms{};};
    std::array<Sample,Capacity> values{};size_t count{},head{};
public:
    void Add(double now,double ms) {if(!std::isfinite(ms) || ms<=0)return;values[head]={now,ms};head=(head+1)%Capacity;count=std::min(count+1,Capacity);}
    void Clear(){count=head=0;}
    HistorySummary Summarize(double now,double seconds) const {
        HistorySummary result;std::vector<double> samples;samples.reserve(count);double sum=0;
        for(size_t i=0;i<count;++i)if(values[i].stamp>=now-seconds*1000 && values[i].stamp<=now) {samples.push_back(values[i].ms);sum+=values[i].ms;}
        if(samples.empty())return result;
        std::sort(samples.begin(),samples.end());result.samples=uint32_t(samples.size());result.average=sum/samples.size();result.minimum=samples.front();result.peak=samples.back();
        auto percentile=[&](double p) {return samples[std::min(samples.size()-1,size_t(std::ceil(p*samples.size())-1))];};
        auto slowMean=[&](double fraction) {const size_t n=std::max<size_t>(1,size_t(std::ceil(fraction*samples.size())));double sum=0;for(size_t i=samples.size()-n;i<samples.size();++i)sum+=samples[i];return 1000.0*n/sum;};
        if(samples.size()>=100) {result.p99=percentile(.99);result.low1=slowMean(.01);}
        if(samples.size()>=1000) {result.p999=percentile(.999);result.low01=slowMean(.001);}
        return result;
    }
    std::array<float,GraphPoints> Graph(double now,double seconds) const {
        std::array<float,GraphPoints> graph{};const auto start=now-seconds*1000;
        for(size_t i=0;i<count;++i)if(values[i].stamp>=start && values[i].stamp<=now) {
            const auto index=std::min(GraphPoints-1,size_t((values[i].stamp-start)/(seconds*1000)*GraphPoints));
            graph[index]=std::max(graph[index],float(values[i].ms));
        }
        return graph;
    }
};
}
