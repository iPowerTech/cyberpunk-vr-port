#include "Render/WorldMarkerShader.hpp"
#include "WorldMarkerShaderCode.hpp"
#include <atomic>
#include <cstring>

extern void Log(const char*,...);

namespace cvr::markers {
namespace {
constexpr unsigned char spriteDigest[16]={0xe5,0xe6,0xff,0x89,0xce,0x8a,0x4e,0xa0,0xca,0xf7,0x78,0x10,0xab,0x46,0x39,0x76};
constexpr unsigned char proceduralDigest[16]={0xe2,0x71,0xd2,0x77,0x6a,0x38,0x1a,0x74,0x49,0xbb,0x72,0x11,0xbe,0xae,0xa6,0x78};
std::atomic<unsigned> installed{0};
std::atomic<bool> failed{false};
int Variant(const D3D12_SHADER_BYTECODE& bc) {
    if(!bc.pShaderBytecode || bc.BytecodeLength<20) return -1;
    const auto* b=static_cast<const unsigned char*>(bc.pShaderBytecode);
    if(std::memcmp(b,"DXBC",4)) return -1;
    if(bc.BytecodeLength==3042 && !std::memcmp(b+4,spriteDigest,16)) return 0;
    if(bc.BytecodeLength==3438 && !std::memcmp(b+4,proceduralDigest,16)) return 1;
    return -1;
}
}
D3D12_SHADER_BYTECODE VertexShaderCode(bool procedural) {
    // Native pixel shaders are SM6. D3D12 rejects a pipeline that mixes them
    // with FXC/SM5 vertex shaders, so compile with DXC at build time and embed.
    return procedural ? D3D12_SHADER_BYTECODE{bytecode::Procedural,sizeof(bytecode::Procedural)}
                      : D3D12_SHADER_BYTECODE{bytecode::Sprite,sizeof(bytecode::Sprite)};
}
bool VertexShaderReplacement(const D3D12_SHADER_BYTECODE& original,D3D12_SHADER_BYTECODE& replacement) {
    const int variant=Variant(original);
    if(variant<0) return false;
    replacement=VertexShaderCode(variant!=0);return true;
}
void VertexShaderResult(const D3D12_SHADER_BYTECODE& original,bool success) {
    const int variant=Variant(original);
    if(variant<0) return;
    if(!success) { failed.store(true,std::memory_order_release);Log("[world-markers] VS pipeline rejected; tagging disabled\n"); }
    else {
        const unsigned bit=1u<<variant;
        if(!(installed.fetch_or(bit,std::memory_order_acq_rel)&bit))
            Log("[world-markers] native ink VS variant %d installed\n",variant);
    }
}
bool VertexShadersReady() { return installed.load(std::memory_order_acquire)==3 && !failed.load(std::memory_order_acquire); }
}
