#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cvr::stereo {struct PipelineStreamInfo;}
namespace cvr::stereo::shader_snapshot {
inline constexpr size_t MaxShaderBytes=16*1024*1024,MaxRootBytes=1024*1024;
inline constexpr UINT MaxInputs=32,MaxSources=16384;
enum class StageCompleteness:uint32_t {Unknown,VsPsOnly,OtherStages};
enum class StreamOutputPresence:uint32_t {Unknown,Absent,Present};
struct CreationContract {
    StageCompleteness stages=StageCompleteness::Unknown;
    StreamOutputPresence streamOutput=StreamOutputPresence::Unknown;
    bool KnownVsPsOnly()const noexcept{return stages==StageCompleteness::VsPsOnly;}
    bool operator==(const CreationContract&)const=default;
};
// Classification of complete creation metadata, not shader resource reflection.
CreationContract DescribeCreation(const D3D12_GRAPHICS_PIPELINE_STATE_DESC&)noexcept;
// Only a successfully parsed complete native stream may supply this metadata.
CreationContract DescribeStreamCreation(const PipelineStreamInfo&)noexcept;
struct Input {
    std::string semantic;
    UINT index{},format{},slot{},offset{},classification{},step{};
};
struct Snapshot {
    uint64_t id{};
    std::vector<uint8_t> vs,ps,root;
    std::vector<Input> input;
    CreationContract creation;
};
// Creation hooks copy immutable source bytes only in explicit observation runs.
// Snapshots own no COM objects and remain usable after the original PSO dies.
void RootCreated(ID3D12RootSignature*,const void*,size_t);
// Legacy partial-bytecode entry: stage and stream-output metadata stay Unknown.
void GraphicsCreated(ID3D12PipelineState*,ID3D12RootSignature*,D3D12_SHADER_BYTECODE,D3D12_SHADER_BYTECODE,const D3D12_INPUT_LAYOUT_DESC&);
void GraphicsCreated(ID3D12PipelineState*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC&);
// Call only after ReadPipelineStream succeeds on the actual creation descriptor.
void GraphicsStreamCreated(ID3D12PipelineState*,const PipelineStreamInfo&);
std::shared_ptr<const Snapshot> Capture(ID3D12PipelineState*);
}
