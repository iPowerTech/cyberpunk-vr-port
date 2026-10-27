#pragma once
#include <d3d12.h>

namespace cvr::stereo {
// Authorizes an additional depth-reference draw. Empty stages are valid;
// unsupported bytecode and any UAV binding fail closed.
bool ShaderReadOnly(D3D12_SHADER_BYTECODE);
bool ExplicitShaderResources(D3D12_SHADER_BYTECODE);
// Native VS/PS routing into one array layer. Rejects writes, ViewID and vertex
// outputs that select an array layer or viewport. Only the PS may be empty.
bool ShaderSingleLayerReadOnly(D3D12_SHADER_BYTECODE,bool vertex);
}
