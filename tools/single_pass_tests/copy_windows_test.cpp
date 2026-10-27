#include "Render/NativeCopyWindows.hpp"
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace cache=cvr::stereo::copy_windows;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(1000);
    auto* resource=reinterpret_cast<ID3D12Resource*>(2000);
    auto window=cache::Create(1000,10),other=cache::Create(1100,11);
    const cache::Span span{2000,0x100000,4096,128,256,512,1};
    window->Publish(span);other->Publish(span);
    Require(!window->Find(2000,128,256,1) && !window->Find(2000,127,256,2) && !window->Find(2000,128,257,2) && !window->Find(2001,128,256,2),"reuse must preserve occurrence, identity and full bounds");
    auto token=window->Find(2000,160,64,2);Require(cache::Valid(token) && token->span.destination+160-token->span.offset==544,"contained immutable range lost its private offset");
    cache::Write(reinterpret_cast<ID3D12Resource*>(3000));Require(cache::Valid(token),"unrelated resource invalidated bytes");
    cache::Address(0x100000+384,1);Require(cache::Valid(token),"touching address interval invalidated bytes");
    std::thread writer([&]{cache::Address(0x100000+383,2);});writer.join();
    Require(!cache::Valid(token) && !window->Find(2000,128,256,2) && !other->Find(2000,128,256,2),"cross-thread address write left stale tokens");
    window->Publish(span);token=window->Find(2000,128,256,2);
    {cache::ReadOnlyScope known;cache::UnknownDraw(list);Require(cache::Valid(token),"audited read-only draw discarded input version");}
    cache::UnknownDraw(list);Require(!cache::Valid(token),"unknown shader did not invalidate source version");
    window->Publish(span);token=window->Find(2000,128,256,2);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=resource;
    cache::Barriers(1,&barrier);Require(!cache::Valid(token),"transition did not invalidate source version");
    window->Publish(span);token=window->Find(2000,128,256,2);barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    cache::Barriers(1,&barrier);Require(!cache::Valid(token),"alias barrier retained a placed version");
    window->Publish(span);token=window->Find(2000,128,256,2);cache::Reset(list);
    Require(!cache::Valid(token) && !window->Find(2000,128,256,3),"recording reset reused an old snapshot");
    window->Publish(span);Require(!window->Find(2000,128,256,2),"closed recording admitted new entries");
    auto fresh=cache::Create(1000,12);Require(!fresh->Find(2000,128,256,2),"new generation inherited old input snapshots");
    fresh->Publish(span);token=fresh->Find(2000,128,256,2);cache::Unavailable();Require(!cache::Valid(token) && !fresh->Find(2000,128,256,3),"missing observation did not fail closed");
    window.reset();other.reset();fresh.reset();Require(!cache::State().active,"closed windows leaked active instrumentation");
    std::puts("PASS same-recording IA copy windows, conservative writes/barriers, reset and concurrent invalidation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
