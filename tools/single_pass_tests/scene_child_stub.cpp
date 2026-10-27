#include "Render/NativeSceneChild.hpp"

extern "C" {__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SceneChildEnable{};}
namespace cvr::stereo::scene_child {
Scope::Scope(uint64_t,uint64_t,uint32_t)noexcept{}
Scope::~Scope()=default;
bool DescribeCurrent(Identity& out)noexcept{out={};return false;}
bool DescribeCurrentAtList(ID3D12GraphicsCommandList*,Identity& out,native_recording::Lease& lease)noexcept{out={};lease={};return false;}
}
