#include "Render/NativeDepthPolicy.hpp"

extern "C" __declspec(dllexport) bool DepthPolicyReadShader(const void* data,size_t bytes){
    return cvr::stereo::ShaderReadOnly({data,bytes});
}
