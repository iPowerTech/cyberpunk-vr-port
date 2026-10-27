#pragma once
#include "Render/SourceSelectorPlan.hpp"
#include <array>

namespace cvr::stereo::multilayer_selectors {
inline constexpr std::string_view RootSha256="030ea7831db8493ad9b03f05ae54aa8d70e27211b65a93f9c9e155a925110a7c";
inline constexpr std::string_view VertexSha256="d13385d2f6f7206586c8927d9df03e9b2c6b83adb27e6d23c7589f7f496e31aa";
inline constexpr std::string_view PixelSha256="9a4947a3ec0eb3d4605d88b663bb27a0bc809fe8a3d91c4ae67886bd77148db8";
inline constexpr uint32_t B4Bytes=228,Stride=128,MaxRecords=32,MaxReadBytes=4096,DeclaredExtent=32768;
enum class Transform:uint32_t {Raw,Low16,High16};
struct SiteDefinition {
    std::string_view createHandleSSA,indexSSA,recordIndexSSA;
    uint32_t fieldOffset{};
    Transform transform{};
};
std::span<const SiteDefinition> AuditedSites();
struct StructuredKey {
    std::string rootSha256;
    uint32_t visibility{},shaderRegister{},space{},parameter{},tableOffset{};
    bool operator==(const StructuredKey&)const=default;
};
struct StructuredView {
    StructuredKey key;
    uint64_t firstElement{},resourceWidth{};
    uint32_t numElements{},structureByteStride{},format{},dimension{},componentMapping{},flags{};
    bool explicitDescription{};
    bool operator==(const StructuredView&)const=default;
};
struct ReadPlan {
    source_selectors::FamilyKey family;
    source_selectors::CbvKey b4Key;
    std::array<uint8_t,B4Bytes> b4Bytes{};
    StructuredView view;
    uint32_t baseRecord{},mask{},firstRecord{},recordSpan{};
    std::vector<uint32_t> records;
    uint64_t resourceOffset{};
    uint32_t byteExtent{};
    bool conservativeRecordSuperset{},originalSiteCoverageComplete{},captureEpochVerified{};
    bool inputContentsFrozen{},copyPermission{},nativeExecutionPermission{},nativeExecutionPermitted{},performanceGainProved{};
    uint32_t nativeCommandsSuppressed{};
};
struct RecordSnapshot {
    StructuredView view;
    uint64_t resourceOffset{};
    std::span<const uint8_t> bytes;
};
// Exact original PS b4 logical view and explicit PS t5 structured view only.
// FPToui truncates finite nonnegative input; other input domains fail closed.
// No wave/pixel simulation, source ownership, state or copy permission is proved.
bool PlanRead(const source_selectors::FamilyKey&,const source_selectors::OwnedCbvBytes&,
    const StructuredView&,ReadPlan& output,std::string& error);
// Exactly the planned compact COM subview. Gaps between enabled mask bits are
// copied if necessary but are never evaluated as possible selector records.
// Result selections carry all ten original site indices for every possible row.
bool Enumerate(const ReadPlan&,const RecordSnapshot&,source_selectors::Result& output,std::string& error);
}
