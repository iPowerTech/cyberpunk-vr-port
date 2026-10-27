#pragma once
#include <cstdint>
#include <span>

namespace cvr::stereo::pair_submission {
struct Source {
    uint64_t list{},generation{},queue{};
    bool sealed{},certified{},submitted{};
};
enum class Order:uint32_t {None,EarlierSubmission,SeparateCallsInBatch};
struct Result {Order order{};uint32_t producerIndex=UINT32_MAX,consumerIndex=UINT32_MAX;};
// Caller supplies actual validated native leases, not historical pointer labels.
// List order in ONE ExecuteCommandLists call is not a completion dependency.
inline Result Check(const Source& producer,const Source& consumer,uint64_t queue,std::span<const uint64_t> lists,bool separateCalls){
    Result result;
    if(!queue || lists.empty() || lists.size()>256 || !producer.list || !consumer.list || producer.list==consumer.list ||
        !producer.generation || !consumer.generation || !producer.sealed || !consumer.sealed || !producer.certified || !consumer.certified ||
        producer.queue!=queue || consumer.queue!=queue || consumer.submitted)return result;
    uint32_t producers{},consumers{};
    for(uint32_t i=0;i<lists.size();++i){if(lists[i]==producer.list){++producers;result.producerIndex=i;}
        if(lists[i]==consumer.list){++consumers;result.consumerIndex=i;}}
    if(consumers!=1)return result;
    if(producer.submitted){if(!producers)result.order=Order::EarlierSubmission;}
    else if(producers==1 && separateCalls && result.producerIndex<result.consumerIndex)result.order=Order::SeparateCallsInBatch;
    return result;
}
}
