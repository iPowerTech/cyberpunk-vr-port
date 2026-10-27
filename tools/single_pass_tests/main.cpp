#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
void Check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) {std::fprintf(stderr,"%s: 0x%08x\n",operation,unsigned(hr));throw std::runtime_error(operation);}
}
int main() try {
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"EnumAdapter");
    DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc),"GetDesc1");
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"D3D12CreateDevice");
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&options,sizeof(options)),"Options3");
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_1};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model)),"ShaderModel");
    D3D12_FEATURE_DATA_D3D12_OPTIONS base{};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&base,sizeof(base)),"Options");
    char name[512]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr);
    std::printf("{\"adapter\":\"%s\",\"vendor\":%u,\"viewInstancingTier\":%u,\"shaderModel61\":%s,\"arrayIndexWithoutGS\":%s,\"bindingTier\":%u}\n",
        name,desc.VendorId,unsigned(options.ViewInstancingTier),model.HighestShaderModel>=D3D_SHADER_MODEL_6_1?"true":"false",
        base.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation?"true":"false",unsigned(base.ResourceBindingTier));
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
