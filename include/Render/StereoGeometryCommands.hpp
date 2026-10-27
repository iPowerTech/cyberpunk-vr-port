#pragma once
#include "Render/NativeGeometryLeases.hpp"
#include <memory>
#include <optional>

namespace cvr::stereo {
struct GeometryCommandPair {
    std::array<geometry::DrawResources,2> eyes;
    bool instanceRebaseSafe{};
};
struct PureIndirectSource {
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> signature;
    UINT maxCommandCount{};
    Microsoft::WRL::ComPtr<ID3D12Resource> arguments;
    UINT64 argumentOffset{};
    Microsoft::WRL::ComPtr<ID3D12Resource> countBuffer;
    UINT64 countOffset{};
};
struct GeometrySourceCommand {
    geometry::DrawResources geometry;
    // Present: native pure DRAW or DRAW_INDEXED signature and exact operands.
    // DrawResources supplies the consumed IA views; its direct draw arguments
    // are ignored. Pure DRAW may omit IB. Absent: exact indexed direct draw.
    std::optional<PureIndirectSource> indirect;
};
struct IndirectCopyProof {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    // Unset is unknown, including when COMMON would numerically be zero.
    std::optional<D3D12_RESOURCE_STATES> state;
    UINT64 recordingGeneration{},commandPosition{};
    Microsoft::WRL::ComPtr<IUnknown> allocationOwner;
};
// Read-only validation of an already acquired current copy/allocation receipt.
// The callback must not register resource use, record commands or mutate state.
using ValidateIndirectCopy=bool(*)(ID3D12GraphicsCommandList*,const IndirectCopyProof&,UINT64 offset,UINT64 bytes);
struct IndirectSnapshotReceipt {
    UINT64 list{},recordingGeneration{},commandPosition{};
    UINT kind{},stride{};
    std::array<UINT64,2> sourceOffsets{},copiedBytes{}; // arguments, optional count
};
struct CapturedIndirectSource {
    PureIndirectSource replay; // owned DEFAULT buffers; both offsets relocate to 0
    IndirectSnapshotReceipt receipt;
    Microsoft::WRL::ComPtr<IUnknown> owner;
};
// Owns geometry references and, for paired Create, a compact immutable instance
// stream. The caller owns input-content/state/alias admission, scheduling, PSOs,
// descriptor banks, targets and dynamic state. Recording does not replace native
// draws or restore state; resource retention alone does not freeze input bytes.
class StereoGeometryCommands {
public:
    static constexpr UINT MaxSourceCommands=2048;
    static constexpr UINT64 MaxIndirectSnapshotBytes=8*1024*1024;
    StereoGeometryCommands();
    ~StereoGeometryCommands();
    HRESULT Create(ID3D12Device*,std::span<const GeometryCommandPair>);
    bool RecordGeometry(ID3D12GraphicsCommandList*,UINT draw) const;
    UINT DrawCount() const;
    UINT64 InstanceBytes() const;
    // Alternative single initialization: exact indexed source draws, including
    // all consumed IA slots and original firstInstance. No pairing, instance
    // copying, compaction, rebasing or deduplication. The caller certifies source
    // contents and allocation lifetime through destination GPU completion.
    HRESULT CreateSources(ID3D12Device*,std::span<const geometry::DrawResources>);
    // Preserves mixed source occurrences without decoding indirect arguments.
    // Native signature classification and padded-stride bounds are validated;
    // COM retention does not establish immutable argument/count/IA contents,
    // read states, physical allocation, producer order or pass admission.
    HRESULT CreateMixedSources(ID3D12Device*,std::span<const GeometrySourceCommand>);
    // GPU-only capture at the caller-certified current source position. Sources
    // are DEFAULT or UPLOAD with explicit current states; the validator proves
    // current recording/copy permission and physical allocation lifetime.
    // Independent versions, no CPU readback/wait or source-position deduplication.
    // All owners are pinned before recording. Failure leaves output unchanged.
    // The caller still proves source-to-destination GPU order and pass admission.
    static HRESULT CaptureIndirectSource(ID3D12GraphicsCommandList*,const PureIndirectSource&,
        const IndirectCopyProof& arguments,const IndirectCopyProof* count,ValidateIndirectCopy,CapturedIndirectSource&);
    bool RecordSourceGeometry(ID3D12GraphicsCommandList*,UINT source) const;
    UINT SourceCount() const;
private:
    HRESULT CreateSourcesImpl(ID3D12Device*,std::span<const geometry::DrawResources>,std::span<const GeometrySourceCommand>);
    struct Storage;
    struct IndirectStorage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
