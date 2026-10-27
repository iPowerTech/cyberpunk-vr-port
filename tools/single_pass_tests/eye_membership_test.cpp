#include "Render/NativeEyeMask.hpp"
#include <stdexcept>
#include <cstdio>
#include <random>
using namespace cvr::stereo::packets;
void Require(bool value){if(!value)throw std::runtime_error("eye membership assertion failed");}
int main()try{
    {
        std::vector<uint8_t> a,b,other;
        Require(GpuInstanceIdentities(10,4096,3,48,a));
        Require(GpuInstanceIdentities(10,4096+48,2,48,b));
        Require(std::equal(b.begin(),b.end(),a.begin()+48));
        Require(GpuInstanceIdentities(11,4096+48,2,48,other) && other!=b);
        const auto before=other;
        Require(!GpuInstanceIdentities(0,4096,1,48,other) && other==before);
        Require(!GpuInstanceIdentities(1,UINT64_MAX-47,1,48,other) && other==before);
        Require(!GpuInstanceIdentities(1,4096,1,64,other));
        EyeMembership gpu;Inputs input{};input.valid=17;input.instances=3;input.instanceStride=48;input.instanceBytes=144;
        const Packet p{0,(uint64_t(1)<<51)|(uint64_t(3)<<18)};
        Require(gpu.Add(0,p,input,a));input.instances=2;input.instanceBytes=96;
        Require(gpu.Add(1,{0,(uint64_t(1)<<51)|(uint64_t(2)<<18)},input,b));
        std::vector<uint8_t> masks;Require(gpu.Plan(input.draw,a,48,masks) && masks==std::vector<uint8_t>({2,3,3}));
    }
    EyeMembership plan;Inputs source{};source.valid=17;source.instances=3;source.instanceStride=48;source.instanceBytes=144;
    source.draw[8]=7;source.draw[0x20]=180;
    Packet p{0,(uint64_t(1)<<51)|(uint64_t(3)<<18)};
    std::vector<uint8_t> blob(144);blob[0]=1;blob[48]=2;blob[96]=2;
    Require(plan.Add(0,p,source,blob));
    auto peer=source;peer.instances=2;peer.instanceBytes=96;auto q=p;q.second^=uint64_t(1)<<18;
    Require(plan.Add(1,q,peer,blob)); // Common first instance and only one copy of the duplicate.
    peer.instances=1;peer.instanceBytes=48;peer.instanceOffset=96;peer.draw[0x20]=57;
    q.second=(uint64_t(1)<<51)|(uint64_t(1)<<18);
    Require(plan.Add(1,q,peer,blob)); // Native peer LOD must not be treated as MAIN's geometry.
    std::vector<uint8_t> masks;auto wrong=blob;wrong[96]=3;
    Require(!plan.Plan(source.draw,wrong,48,masks) && masks.empty()); // Failure does not consume entries.
    auto padding=source.draw;padding[1]=1;padding[63]=9;
    Require(plan.Plan(padding,blob,48,masks) && masks==std::vector<uint8_t>({3,3,2}));
    Require(!plan.Plan(source.draw,blob,48,masks));
    Require(plan.totals[0]==3 && plan.totals[1]==3);
    Require(plan.KnownExtras()==1);std::vector<uint8_t> extra;
    Require(plan.Extras(source.draw,48,extra) && extra.empty());
    Require(plan.Extras(peer.draw,48,extra) && extra==std::vector<uint8_t>(blob.begin()+96,blob.end()));
    Require(plan.Extras(peer.draw,48,extra) && extra.empty());
    auto invalid=source;invalid.valid=1;Require(plan.Add(1,p,invalid,blob) && plan.unresolved[1]==1);
    invalid=source;invalid.instanceBytes=143;Require(!plan.Add(1,p,invalid,blob));
    invalid=source;invalid.instances=UINT32_MAX;Require(!plan.Add(1,p,invalid,blob));
    invalid=source;invalid.draw[0]=1;Require(plan.Add(1,p,invalid,blob));
    Require(!plan.Add(2,p,source,blob));
    EyeMembership single;source.instances=1;source.instanceBytes=48;p=q;
    Require(single.Add(0,p,source,blob));Require(single.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({2}));
    EyeMembership holes;Require(holes.Add(0,p,source,blob));
    auto hole=source;hole.valid=1;Require(holes.Add(1,p,hole,{}));
    Require(!holes.Plan(source.draw,{blob.data(),48},48,masks));
    Require(holes.Add(1,p,source,blob)); // Known positive coverage remains usable.
    Require(holes.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({3}));
    hole.draw[8]=9;EyeMembership unrelated;Require(unrelated.Add(0,p,source,blob));Require(unrelated.Add(1,p,hole,{}));
    Require(unrelated.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({2}));
    EyeMembership repeated;Require(repeated.Add(0,p,source,blob));
    auto three=source;three.instances=3;three.instanceBytes=144;auto threePacket=p;threePacket.second|=uint64_t(2)<<18;
    std::vector<uint8_t> same;for(int i=0;i<3;++i)same.insert(same.end(),blob.begin(),blob.begin()+48);
    Require(repeated.Add(1,threePacket,three,same));Require(repeated.KnownExtras()==2);
    Require(repeated.Plan(source.draw,{blob.data(),48},48,masks));
    Require(repeated.Extras(source.draw,48,extra) && extra.size()==96);
    EyeMembership unknownMain;Require(unknownMain.Add(1,threePacket,three,same));hole=source;hole.valid=1;
    Require(unknownMain.Add(0,p,hole,{}));Require(!unknownMain.KnownExtras());
    Require(!unknownMain.Extras(source.draw,48,extra));
    EyeMembership partialMain;Require(partialMain.Add(0,p,source,blob));Require(partialMain.Add(0,p,hole,{}));
    Require(partialMain.Add(1,threePacket,three,same));
    Require(partialMain.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({3}));
    Require(!partialMain.Extras(source.draw,48,extra) && !partialMain.KnownExtras());
    EyeMembership offsets;auto otherOffset=source;otherOffset.draw[0x1c]=91;
    Require(offsets.Add(0,p,source,blob));Require(offsets.Add(1,p,otherOffset,blob));
    Require(!offsets.KnownExtras());Require(offsets.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({3}));
    EyeMembership activeOffset;source.draw[0x10]=otherOffset.draw[0x10]=3;
    Require(activeOffset.Add(0,p,source,blob));Require(activeOffset.Add(1,p,otherOffset,blob));
    Require(activeOffset.KnownExtras()==1);Require(activeOffset.Plan(source.draw,{blob.data(),48},48,masks) && masks==std::vector<uint8_t>({2}));
    EyeMembership groups;
    auto add=[&](EyeMembership& target,unsigned side,uint8_t geometry,uint8_t value,uint32_t count=1,uint32_t stride=48){
        Inputs input{};input.valid=17;input.instances=count;input.instanceStride=stride;input.instanceBytes=count*stride;input.draw[8]=geometry;
        Packet packet{0,(uint64_t(1)<<51)|(uint64_t(count)<<18)|(stride==64?(uint64_t(1)<<50):0)};
        std::vector<uint8_t> values(input.instanceBytes);
        for(uint32_t i=0;i<count;++i)values[i*stride]=value;
        Require(target.Add(side,packet,input,values));return input.draw;
    };
    // Interleaved geometries must retain first-peer order and duplicate counts.
    for(uint8_t g=64;g;--g)add(groups,1,g,2);
    for(uint8_t g=1;g<=64;++g){add(groups,1,g,1);add(groups,1,g,2);if(!(g&1))add(groups,0,g,2);}
    for(uint8_t g=1;g<=64;++g){std::array<uint8_t,64> draw{};draw[8]=g;
        std::vector<uint8_t> expected((g&1)?144:96);expected[0]=2;
        if(g&1)expected[48]=2;expected[expected.size()-48]=1;
        Require(groups.Extras(draw,48,extra) && extra==expected);
        Require(groups.Extras(draw,48,extra) && extra.empty());}
    EyeMembership mixed;const auto geometry=add(mixed,1,77,3);add(mixed,1,77,4,1,64);
    Require(mixed.Extras(geometry,48,extra) && extra.size()==48 && extra[0]==3);
    Require(mixed.Extras(geometry,64,extra) && extra.size()==64 && extra[0]==4);
    EyeMembership tooLarge;const auto large=add(tooLarge,1,78,8);add(tooLarge,1,78,9,400);
    extra={111};Require(!tooLarge.Extras(large,48,extra) && extra==std::vector<uint8_t>({111}));
    add(tooLarge,0,78,9,100);Require(tooLarge.Extras(large,48,extra) && extra.size()==301*48 && extra[0]==8 && extra[48]==9);
    // Differential check against the original hash-debit algorithm, including
    // failed queries followed by valid ones and mixed duplicate ordering.
    std::mt19937 random(197);
    for(unsigned trial=0;trial<100;++trial){
        EyeMembership actual;std::unordered_map<std::string,std::array<uint32_t,2>> reference;
        Inputs in{};in.valid=17;in.instances=96;in.instanceStride=trial&1?64:48;in.instanceBytes=in.instances*in.instanceStride;in.draw[8]=42;
        Packet packet{0,(uint64_t(1)<<51)|(uint64_t(in.instances)<<18)|(in.instanceStride==64?(uint64_t(1)<<50):0)};
        for(unsigned side=0;side<2;++side){
            std::vector<uint8_t> bytes(in.instanceBytes);
            for(unsigned i=0;i<in.instances;++i){bytes[i*in.instanceStride]=uint8_t(random()%12);std::string key;
                Require(GeometryInstanceKey(in.draw,{bytes.data()+i*in.instanceStride,in.instanceStride},key));++reference[key][side];}
            Require(actual.Add(side,packet,in,bytes));
        }
        const bool unread=trial%3==0;
        if(unread){auto missing=in;missing.valid=1;Require(actual.Add(1,packet,missing,{}));}
        for(unsigned query=0;query<32;++query){
            const auto count=1+random()%12;std::vector<uint8_t> bytes(count*in.instanceStride);
            std::unordered_map<std::string,uint32_t> debit;std::vector<uint8_t> expected;bool ok=true;
            for(unsigned i=0;i<count;++i)bytes[i*in.instanceStride]=uint8_t(random()%13);
            for(unsigned i=0;i<count;++i){std::string key;Require(GeometryInstanceKey(in.draw,{bytes.data()+i*in.instanceStride,in.instanceStride},key));
                const auto found=reference.find(key);const auto used=debit[key]++;
                if(found==reference.end() || found->second[0]<=used){ok=false;break;}
                const bool common=found->second[1]>used;
                if(!common && unread){ok=false;break;}
                expected.push_back(common?3:2);
            }
            std::vector<uint8_t> result{123};Require(actual.Plan(in.draw,bytes,in.instanceStride,result)==ok);
            if(ok){Require(result==expected);for(const auto& [key,n]:debit){auto& c=reference.at(key);c[0]-=n;c[1]-=std::min(c[1],n);}}
            else Require(result==std::vector<uint8_t>({123}));
        }
    }
    std::printf("PASS early per-instance geometry membership, native LODs, duplicates and transactional selection\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
