#pragma once
#include <d3d12.h>

namespace cvr::markers {
// The two captured flat ink VS variants, with their existing bindings/signatures.
bool VertexShaderReplacement(const D3D12_SHADER_BYTECODE& original, D3D12_SHADER_BYTECODE& replacement);
void VertexShaderResult(const D3D12_SHADER_BYTECODE& original, bool success);
bool VertexShadersReady();
D3D12_SHADER_BYTECODE VertexShaderCode(bool procedural);
}
