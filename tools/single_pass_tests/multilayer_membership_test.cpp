#include "Render/GpuMultilayerMembership.hpp"
#include "Render/GpuGeometryCompare.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 multilayer membership: "+std::to_string(uint32_t(hr)));}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));return result;
}
static void Upload(ID3D12Resource* resource,const void* bytes,size_t count){
    void* mapped{};const D3D12_RANGE none{};Check(resource->Map(0,&none,&mapped));std::memcpy(mapped,bytes,count);
    const D3D12_RANGE written{0,count};resource->Unmap(0,&written);
}
static void Store(std::vector<uint8_t>& bytes,size_t offset,UINT value){
    Require(offset<=bytes.size() && bytes.size()-offset>=4,"Fixture DWORD bounds");std::memcpy(bytes.data()+offset,&value,4);
}
static UINT64 Qword(const uint8_t* bytes,size_t offset){UINT64 value{};std::memcpy(&value,bytes+offset,8);return value;}
static UINT Word(const uint8_t* bytes,size_t offset){UINT value{};std::memcpy(&value,bytes+offset,4);return value;}
struct Case {
    std::string name;float base{};UINT mask=1,first{},elements=8192;
    std::array<UINT,5> indices{3,5,7,11,13};
    int missing=-1;
    UINT error{},records=1,selectors=5,failedBit=UINT_MAX,failedIndex=UINT_MAX;
    bool changeIndex{},changeBase{};
};
static std::vector<Case> Cases(){
    std::vector<Case> cases;
    cases.push_back({"single"});
    Case value{"zero mask"};value.mask=0;value.records=value.selectors=0;cases.push_back(value);
    value={"sparse high bits"};value.base=3;value.mask=0x80000081u;value.first=2;value.elements=8190;value.records=3;value.selectors=15;cases.push_back(value);
    value={"bit31"};value.mask=0x80000000u;cases.push_back(value);
    value={"all mask bits"};value.base=1;value.mask=UINT_MAX;value.records=32;value.selectors=160;cases.push_back(value);
    value={"fractional truncation"};value.base=3.75f;cases.push_back(value);
    value={"negative zero"};value.base=-0.0f;cases.push_back(value);
    value={"last record and descriptor"};value.base=8191;value.indices.fill(32767);cases.push_back(value);
    value={"nonzero original FirstElement"};value.base=7;value.first=17;value.elements=8;cases.push_back(value);
    for(const auto raw:{-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),4294967296.0f}){
        value={"invalid float base"};value.base=raw;value.error=GpuMultilayerMembership::InvalidBase;value.records=value.selectors=0;cases.push_back(value);}
    value={"zero mask invalid base conservatively refused"};value.base=std::numeric_limits<float>::quiet_NaN();value.mask=0;
    value.error=GpuMultilayerMembership::InvalidBase;value.records=value.selectors=0;cases.push_back(value);
    value={"base outside T5"};value.base=8192;value.error=GpuMultilayerMembership::ViewBounds;value.records=value.selectors=0;value.failedBit=0;cases.push_back(value);
    value={"bit exceeds T5"};value.base=8191;value.mask=2;value.error=GpuMultilayerMembership::ViewBounds;value.records=value.selectors=0;value.failedBit=1;cases.push_back(value);
    value={"largest uint-convertible float"};value.base=4294967040.0f;value.mask=0x80000000u;value.error=GpuMultilayerMembership::ViewBounds;
    value.records=value.selectors=0;value.failedBit=31;cases.push_back(value);
    value={"nonzero FirstElement view bounds"};value.base=8;value.first=17;value.elements=8;value.error=GpuMultilayerMembership::ViewBounds;
    value.records=value.selectors=0;value.failedBit=0;cases.push_back(value);
    for(UINT i=0;i<5;++i){value={"missing selector "+std::to_string(i)};value.missing=int(i);value.error=GpuMultilayerMembership::MissingDescriptor;
        value.failedBit=0;value.failedIndex=value.indices[i];cases.push_back(value);}
    for(UINT i=0;i<5;++i){value={"out of range selector "+std::to_string(i)};value.indices[i]=i==0?0x80000005u:32768u;
        value.error=GpuMultilayerMembership::DescriptorBounds;value.failedBit=0;value.failedIndex=value.indices[i];cases.push_back(value);}
    value={"GPU changes selector to another bank member"};value.changeIndex=true;cases.push_back(value);
    value={"GPU changes B4 base to another actual record"};value.changeBase=true;cases.push_back(value);
    return cases;
}
static void RecordRow(std::vector<uint8_t>& data,size_t at,const std::array<UINT,5>& index){
    Store(data,at+100,index[0]);Store(data,at+104,index[3]|(index[4]<<16));Store(data,at+108,(index[1]<<16)|index[2]);
}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::string(argv[1])=="--hardware";
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){for(UINT i=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++i){
        DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    const auto cases=Cases();constexpr UINT T5Bytes=1024*1024,T5Stride=T5Bytes+256;
    const UINT count=UINT(cases.size());std::vector<uint8_t> b4Bytes(count*256),t5Bytes(size_t(count)*T5Stride,0xff);
    auto b4=Buffer(device.Get(),b4Bytes.size(),D3D12_HEAP_TYPE_DEFAULT),t5=Buffer(device.Get(),t5Bytes.size(),D3D12_HEAP_TYPE_DEFAULT);
    auto b4Upload=Buffer(device.Get(),b4Bytes.size(),D3D12_HEAP_TYPE_UPLOAD),t5Upload=Buffer(device.Get(),t5Bytes.size(),D3D12_HEAP_TYPE_UPLOAD);
    auto gate=Buffer(device.Get(),16,D3D12_HEAP_TYPE_DEFAULT),gateUpload=Buffer(device.Get(),16,D3D12_HEAP_TYPE_UPLOAD);
    const UINT64 gates[]{0,UINT64(1)<<32};Upload(gateUpload.Get(),gates,sizeof(gates));
    auto mutations=Buffer(device.Get(),16,D3D12_HEAP_TYPE_UPLOAD);const UINT mutationValues[]{17,std::bit_cast<UINT>(2.0f),0,0};
    Upload(mutations.Get(),mutationValues,sizeof(mutationValues));
    std::vector<MultilayerMembershipSource> sources;
    for(UINT i=0;i<count;++i){const auto& test=cases[i];MultilayerMembershipSource source;
        source.family={std::string(multilayer_selectors::RootSha256),std::string(multilayer_selectors::VertexSha256),std::string(multilayer_selectors::PixelSha256)};
        source.b4=b4;source.t5=t5;source.b4Offset=UINT64(i)*256+4;source.t5Offset=UINT64(i)*T5Stride+128;
        source.t5Bytes=T5Bytes;source.firstElement=test.first;source.numElements=test.elements;source.sourceId=i+1;source.eye=i%2;
        Store(b4Bytes,size_t(source.b4Offset)+128,std::bit_cast<UINT>(test.base));Store(b4Bytes,size_t(source.b4Offset)+224,test.mask);
        if(std::isfinite(test.base) && test.base>=0 && double(test.base)<4294967296.0){const UINT base=UINT(test.base);
            for(UINT bit=0;bit<32;++bit)if((test.mask&(1u<<bit)) && UINT64(base)+bit<test.elements)
                RecordRow(t5Bytes,size_t(source.t5Offset)+(size_t(test.first)+base+bit)*128,test.indices);}
        if(test.changeBase)RecordRow(t5Bytes,size_t(source.t5Offset)+2*128,test.indices);
        for(UINT selector=0;selector<5;++selector)if(int(selector)!=test.missing && test.indices[selector]<32768)
            source.membership[test.indices[selector]>>5]|=1u<<(test.indices[selector]&31);
        if(test.changeIndex)source.membership[17>>5]|=1u<<(17&31);
        sources.push_back(std::move(source));
    }
    Upload(b4Upload.Get(),b4Bytes.data(),b4Bytes.size());Upload(t5Upload.Get(),t5Bytes.data(),t5Bytes.size());
    GpuMultilayerMembership enabled,disabled;
    Check(enabled.Create(device.Get(),sources,gate.Get(),8));Check(disabled.Create(device.Get(),sources,gate.Get()));
    Require(!enabled.DispatchCount() && !disabled.DispatchCount(),"Unrecorded memberships reported dispatches");
    constexpr UINT duplicateB4Offset=512,duplicateT5Offset=256;
    auto duplicateB4=Buffer(device.Get(),b4Bytes.size()+duplicateB4Offset,D3D12_HEAP_TYPE_DEFAULT);
    auto duplicateT5=Buffer(device.Get(),t5Bytes.size()+duplicateT5Offset,D3D12_HEAP_TYPE_DEFAULT);
    // Interleave the first-seen two resource pairs; output rows must preserve
    // this order even though dispatch metadata is grouped A,A,A then B,B,B.
    const std::array<UINT,6> groupCases{0,2,7,8,18,5};std::vector<MultilayerMembershipSource> groupedSources;
    for(UINT i=0;i<groupCases.size();++i){auto source=sources[groupCases[i]];
        if(i&1){source.b4=duplicateB4;source.t5=duplicateT5;source.b4Offset+=duplicateB4Offset;source.t5Offset+=duplicateT5Offset;}
        groupedSources.push_back(std::move(source));
    }
    std::array<GpuMultilayerMembership,2> grouped;
    Check(grouped[0].Create(device.Get(),groupedSources,gate.Get(),8));Check(grouped[1].Create(device.Get(),groupedSources,gate.Get()));
    Require(!grouped[0].DispatchCount() && !grouped[1].DispatchCount(),"Created grouped membership already recorded");
    const auto reject=[&](std::span<const MultilayerMembershipSource> input,ID3D12Resource* permission= nullptr,UINT64 offset=0){
        GpuMultilayerMembership invalid;Require(FAILED(invalid.Create(device.Get(),input,permission?permission:gate.Get(),offset)),"Invalid membership metadata accepted");
        Require(!invalid.Predicates() && !invalid.Results() && !invalid.SourceCount() && !invalid.Record(list.Get()),"Invalid membership object recorded commands");};
    reject({});std::vector<MultilayerMembershipSource> excessive(513,sources[0]);reject(excessive);
    for(UINT which=0;which<3;++which){auto bad=sources[0];
        (which==0?bad.family.rootSha256:which==1?bad.family.vertexSha256:bad.family.pixelSha256)="wrong";reject({&bad,1});}
    for(UINT which=0;which<11;++which){auto bad=sources[0];
        switch(which){case 0:bad.b4Offset=1;break;case 1:bad.b4Offset=b4Bytes.size()-224;break;
        case 2:bad.t5Offset=UINT64(UINT_MAX)+1;break;case 3:bad.t5Bytes=0;break;case 4:bad.t5Bytes=T5Bytes+128;break;
        case 5:bad.t5Bytes=129;break;case 6:bad.firstElement=8192;break;case 7:bad.numElements=0;break;
        case 8:bad.numElements=UINT_MAX;break;case 9:bad.eye=2;break;case 10:bad.sourceId=0;break;}
        reject({&bad,1});
    }
    auto missingResource=sources[0];missingResource.b4.Reset();reject({&missingResource,1});
    missingResource=sources[0];missingResource.t5.Reset();reject({&missingResource,1});
    auto deny=Buffer(device.Get(),256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    auto wrongResource=sources[0];wrongResource.b4=deny;reject({&wrongResource,1});
    reject(sources,gate.Get(),1);reject(sources,gate.Get(),16);
    GpuMultilayerMembership noGate;Require(FAILED(noGate.Create(device.Get(),sources,nullptr)),"Null producer accepted");
    Require(FAILED(enabled.Create(device.Get(),sources,gate.Get())) && !enabled.Record(nullptr),"Repeated Create or null list accepted");
    std::array<GpuMultilayerMembership,3> chained;
    Check(chained[0].Create(device.Get(),std::span(sources).first(1),enabled.Predicates(),GpuMultilayerMembership::PredicateOffset(0)));
    Check(chained[1].Create(device.Get(),std::span(sources).first(1),enabled.Predicates(),GpuMultilayerMembership::PredicateOffset(18,true)));
    Check(chained[2].Create(device.Get(),std::span(sources).first(1),disabled.Predicates(),GpuMultilayerMembership::PredicateOffset(0)));
    auto literalOnes=Buffer(device.Get(),UINT64(count)*8,D3D12_HEAP_TYPE_UPLOAD);
    const std::vector<UINT64> ones(count,1);Upload(literalOnes.Get(),ones.data(),ones.size()*8);
    std::array<std::vector<GeometryCompareRange>,3> andRanges;
    UINT acceptedSources{};
    for(UINT i=0;i<count;++i){GeometryCompareRange range;range.resources={enabled.Predicates(),literalOnes};
        range.offsets={GpuMultilayerMembership::PredicateOffset(i),UINT64(i)*8};range.bytes=8;
        if(!cases[i].error){andRanges[0].push_back(range);++acceptedSources;}
        andRanges[1].push_back(range);range.resources[0]=disabled.Predicates();andRanges[2].push_back(std::move(range));
    }
    Require(acceptedSources && acceptedSources<count,"AND fixture needs accepted and rejected membership sources");
    std::array<GpuGeometryCompare,3> aggregate;
    for(UINT i=0;i<3;++i)Check(aggregate[i].Create(device.Get(),1,andRanges[i],gate.Get(),i==2?0:8));
    const UINT64 predicateBytes=UINT64(count)*16,resultBytes=UINT64(count)*32;
    const UINT64 disabledPred=predicateBytes,resultsAt=predicateBytes*2,disabledResultsAt=resultsAt+resultBytes;
    const UINT64 branchesAt=disabledResultsAt+resultBytes,disabledBranchesAt=branchesAt+predicateBytes,chainsAt=disabledBranchesAt+predicateBytes;
    const UINT64 aggregateAt=chainsAt+3*16,aggregateBranchesAt=aggregateAt+3*16;
    const UINT64 groupedAt=aggregateBranchesAt+3*16,groupedOneBytes=groupCases.size()*48;
    const UINT64 totalBytes=groupedAt+2*groupedOneBytes;
    auto readback=Buffer(device.Get(),totalBytes,D3D12_HEAP_TYPE_READBACK),zero=Buffer(device.Get(),totalBytes,D3D12_HEAP_TYPE_UPLOAD);
    std::vector<uint8_t> zeros(size_t(totalBytes),0);Upload(zero.Get(),zeros.data(),zeros.size());
    auto marker=Buffer(device.Get(),8,D3D12_HEAP_TYPE_UPLOAD);const UINT64 markerValue=0x123456789ABCDEF0;Upload(marker.Get(),&markerValue,8);
    list->CopyBufferRegion(readback.Get(),0,zero.Get(),0,totalBytes);
    list->CopyBufferRegion(b4.Get(),0,b4Upload.Get(),0,b4Bytes.size());list->CopyBufferRegion(t5.Get(),0,t5Upload.Get(),0,t5Bytes.size());
    list->CopyBufferRegion(duplicateB4.Get(),duplicateB4Offset,b4Upload.Get(),0,b4Bytes.size());
    list->CopyBufferRegion(duplicateT5.Get(),duplicateT5Offset,t5Upload.Get(),0,t5Bytes.size());
    list->CopyBufferRegion(gate.Get(),0,gateUpload.Get(),0,16);
    // Mutations execute after Create and before validation on the same GPU list.
    for(UINT i=0;i<count;++i){
        if(cases[i].changeIndex)list->CopyBufferRegion(t5.Get(),sources[i].t5Offset+100,mutations.Get(),0,4);
        if(cases[i].changeBase)list->CopyBufferRegion(b4.Get(),sources[i].b4Offset+128,mutations.Get(),4,4);
    }
    std::array<D3D12_RESOURCE_BARRIER,5> barriers{};
    for(UINT i=0;i<3;++i){barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition={i==0?b4.Get():i==1?t5.Get():gate.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|(i==2?D3D12_RESOURCE_STATE_PREDICATION:0))};}
    for(UINT i=3;i<5;++i){barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition={i==3?duplicateB4.Get():duplicateT5.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};}
    list->ResourceBarrier(UINT(barriers.size()),barriers.data());
    list->SetPredication(gate.Get(),0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    Require(enabled.Record(list.Get()) && disabled.Record(list.Get()),"Membership recording failed");
    Require(!enabled.Record(list.Get()),"Membership recorded twice");
    for(UINT i=0;i<2;++i){Require(grouped[i].Record(list.Get()) && grouped[i].DispatchCount()==2,"Two resource pairs did not create two grouped dispatches");
        Require(!grouped[i].Record(list.Get()),"Grouped membership recorded twice");
        const auto destination=groupedAt+i*groupedOneBytes;
        list->CopyBufferRegion(readback.Get(),destination,grouped[i].Predicates(),0,groupCases.size()*16);
        list->CopyBufferRegion(readback.Get(),destination+groupCases.size()*16,grouped[i].Results(),0,groupCases.size()*32);
    }
    list->CopyBufferRegion(readback.Get(),0,enabled.Predicates(),0,predicateBytes);
    list->CopyBufferRegion(readback.Get(),disabledPred,disabled.Predicates(),0,predicateBytes);
    list->CopyBufferRegion(readback.Get(),resultsAt,enabled.Results(),0,resultBytes);
    list->CopyBufferRegion(readback.Get(),disabledResultsAt,disabled.Results(),0,resultBytes);
    for(UINT i=0;i<3;++i){Require(chained[i].Record(list.Get()),"GPU predicate composition failed");
        list->CopyBufferRegion(readback.Get(),chainsAt+i*16,chained[i].Predicates(),0,16);}
    // The accepted predicates are produced earlier on this SAME list. Comparing
    // each with literal uint64(1) forms a global AND without CPU mapping/waits.
    for(UINT i=0;i<3;++i){Require(aggregate[i].Record(list.Get()),"Membership-to-global-AND recording failed");
        list->CopyBufferRegion(readback.Get(),aggregateAt+i*16,aggregate[i].Predicates(),0,16);}
    for(UINT i=0;i<3;++i)for(UINT branch=0;branch<2;++branch){
        list->SetPredication(aggregate[i].Predicates(),GpuGeometryCompare::Offset(0,branch!=0),D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),aggregateBranchesAt+i*16+branch*8,marker.Get(),0,8);
    }
    for(UINT i=0;i<count;++i)for(UINT branch=0;branch<2;++branch){
        list->SetPredication(enabled.Predicates(),GpuMultilayerMembership::PredicateOffset(i,branch!=0),D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),branchesAt+i*16+branch*8,marker.Get(),0,8);
        list->SetPredication(disabled.Predicates(),GpuMultilayerMembership::PredicateOffset(i,branch!=0),D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),disabledBranchesAt+i*16+branch*8,marker.Get(),0,8);
    }
    list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);Check(list->Close());
    ID3D12CommandList* raw=list.Get();queue->ExecuteCommandLists(1,&raw);ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");
    Check(fence->SetEventOnCompletion(1,event));const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Membership GPU timeout");
    void* mapped{};const D3D12_RANGE read{0,SIZE_T(totalBytes)};Check(readback->Map(0,&read,&mapped));const auto* bytes=static_cast<const uint8_t*>(mapped);
    for(UINT i=0;i<count;++i){const auto& test=cases[i];const bool accepted=!test.error;
        const auto fail=[&](bool value,const char* reason){if(!value)throw std::runtime_error(test.name+": "+reason);};
        fail(Qword(bytes,i*16)==UINT64(accepted) && Qword(bytes,i*16+8)==UINT64(!accepted),"accept/reject predicates");
        fail(!Qword(bytes,disabledPred+i*16) && !Qword(bytes,disabledPred+i*16+8),"disabled predicates");
        const auto at=size_t(resultsAt+i*32);const UINT base=test.error==GpuMultilayerMembership::InvalidBase?0:UINT(test.changeBase?2:test.base);
        fail(Word(bytes,at)==1 && Word(bytes,at+4)==test.error && Word(bytes,at+8)==base && Word(bytes,at+12)==test.mask,"actual GPU input report");
        fail(Word(bytes,at+16)==test.records && Word(bytes,at+20)==test.selectors,"coverage counts");
        fail(Word(bytes,at+24)==test.failedBit && Word(bytes,at+28)==test.failedIndex,"first failure report");
        const auto disabledAt=size_t(disabledResultsAt+i*32);
        for(UINT field=0;field<6;++field)fail(!Word(bytes,disabledAt+field*4),"disabled validation accessed input");
        fail(Word(bytes,disabledAt+24)==UINT_MAX && Word(bytes,disabledAt+28)==UINT_MAX,"disabled diagnostics");
        fail(Qword(bytes,branchesAt+i*16)==(accepted?markerValue:0) && Qword(bytes,branchesAt+i*16+8)==(accepted?0:markerValue),"actual conditional copy branches");
        fail(!Qword(bytes,disabledBranchesAt+i*16) && !Qword(bytes,disabledBranchesAt+i*16+8),"disabled actual branches executed");
    }
    for(UINT run=0;run<2;++run)for(UINT i=0;i<groupCases.size();++i){const auto& test=cases[groupCases[i]];
        const auto at=groupedAt+run*groupedOneBytes,diagnostic=at+groupCases.size()*16+i*32;
        const bool accepted=!test.error;
        Require(Qword(bytes,at+i*16)==UINT64(!run && accepted) && Qword(bytes,at+i*16+8)==UINT64(!run && !accepted),
            "Grouped predicates lost original input order");
        if(!run){
            Require(Word(bytes,diagnostic)==1 && Word(bytes,diagnostic+4)==test.error && Word(bytes,diagnostic+8)==UINT(test.base) &&
                Word(bytes,diagnostic+12)==test.mask && Word(bytes,diagnostic+16)==test.records && Word(bytes,diagnostic+20)==test.selectors &&
                Word(bytes,diagnostic+24)==test.failedBit && Word(bytes,diagnostic+28)==test.failedIndex,
                "Grouped metadata selected another source resource, offset or output index");
        }else{
            for(UINT field=0;field<6;++field)Require(!Word(bytes,diagnostic+field*4),"Disabled grouped membership accessed input");
            Require(Word(bytes,diagnostic+24)==UINT_MAX && Word(bytes,diagnostic+28)==UINT_MAX,"Disabled grouped diagnostics changed");
        }
    }
    Require(Qword(bytes,chainsAt)==1 && !Qword(bytes,chainsAt+8),"accepted predicate did not compose");
    Require(Qword(bytes,chainsAt+16)==1 && !Qword(bytes,chainsAt+24),"rejected predicate did not compose");
    Require(!Qword(bytes,chainsAt+32) && !Qword(bytes,chainsAt+40),"zero composed predicate enabled rejection");
    Require(Qword(bytes,aggregateAt)==1 && !Qword(bytes,aggregateAt+8),"All accepted membership sources did not AND to acceptance");
    Require(!Qword(bytes,aggregateAt+16) && Qword(bytes,aggregateAt+24)==1,"One rejected membership source did not reject the global AND");
    Require(!Qword(bytes,aggregateAt+32) && !Qword(bytes,aggregateAt+40),"Zero producer enabled either global-AND branch");
    Require(Qword(bytes,aggregateBranchesAt)==markerValue && !Qword(bytes,aggregateBranchesAt+8),"Accepted AND did not exclusively execute acceptance consumer");
    Require(!Qword(bytes,aggregateBranchesAt+16) && Qword(bytes,aggregateBranchesAt+24)==markerValue,"Mixed AND did not exclusively execute refusal consumer");
    Require(!Qword(bytes,aggregateBranchesAt+32) && !Qword(bytes,aggregateBranchesAt+40),"Disabled AND executed a consumer");
    for(UINT i=0;i<3;++i)Require(aggregate[i].PairCount()==1 && aggregate[i].RangeCount()==(i?count:acceptedSources) &&
        aggregate[i].ComparedBytes()==UINT64(i?count:acceptedSources)*8 && aggregate[i].DispatchCount(),"Membership AND accounting");
    const D3D12_RANGE none{};readback->Unmap(0,&none);
    Require(enabled.SourceCount()==count && enabled.DispatchCount()==1 && disabled.DispatchCount()==1 && enabled.ReadbackBytes()==count*48,"Membership accounting");
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};Check(messages->GetMessage(i,nullptr,&size));
        std::vector<uint8_t> data(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(messages->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("Membership D3D12 validation error");}}
    cvr::gpu::ResetCommandResources(list.Get());
    std::printf("PASS %s actual-GPU multilayer membership: %u sources in one dispatch, six interleaved sources in two dispatches, original-index full T5, zero producer and global AND consumers\n",hardware?"hardware":"WARP",count);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
