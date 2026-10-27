#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace cvr::stereo::source_selectors {
inline constexpr uint32_t DeclaredExtent=32768,MaxFamilies=64,MaxSites=256,MaxTotalSites=4096;
inline constexpr size_t MaxCatalogBytes=1024*1024;
struct FamilyKey {
    std::string rootSha256,vertexSha256,pixelSha256;
    bool operator==(const FamilyKey&)const=default;
    bool operator<(const FamilyKey& b)const{return std::tie(rootSha256,vertexSha256,pixelSha256)<std::tie(b.rootSha256,b.vertexSha256,b.pixelSha256);}
};
struct CbvKey {
    std::string rootSha256;
    uint32_t visibility{},shaderRegister{},space{},parameter{},tableOffset{};
    bool operator==(const CbvKey&)const=default;
    bool operator<(const CbvKey& b)const{return std::tie(rootSha256,visibility,shaderRegister,space,parameter,tableOffset)<
        std::tie(b.rootSha256,b.visibility,b.shaderRegister,b.space,b.parameter,b.tableOffset);}
};
struct TableKey {
    std::string rootSha256;
    uint32_t visibility{},parameter{},space{},shaderRegisterBase{},originalTableOffset{},declaredExtent{};
    bool operator==(const TableKey&)const=default;
    bool operator<(const TableKey& b)const{return std::tie(rootSha256,visibility,parameter,space,shaderRegisterBase,originalTableOffset,declaredExtent)<
        std::tie(b.rootSha256,b.visibility,b.parameter,b.space,b.shaderRegisterBase,b.originalTableOffset,b.declaredExtent);}
};
struct CbvRequirement {
    CbvKey key;
    uint32_t reflectedBytes{},requiredByteExtent{};
    std::vector<uint32_t> dwordOffsets;
};
struct Site {
    std::string shaderSha256,createHandleSSA,indexSSA;
    uint32_t createHandleLine{},byteOffset{};
    CbvKey cbv;
    TableKey table;
};
struct FamilyPlan {
    FamilyKey key;
    uint32_t number{};
    bool supported{};
    std::string unsupportedReason;
    std::vector<std::string> unresolvedInputs;
    std::vector<CbvRequirement> cbvs;
    std::vector<Site> sites;
};
struct Catalog {
    std::string evidenceSha256;
    std::vector<FamilyPlan> families;
    const FamilyPlan* Find(const FamilyKey&)const;
};
struct OwnedCbvBytes {
    CbvKey key;
    // Exact logical view bytes; ownership and capture epoch belong to the caller.
    std::span<const uint8_t> bytes;
};
using CbvBytes=OwnedCbvBytes;
struct SlotInterval {uint32_t offset{},count{},type{};bool operator==(const SlotInterval&)const=default;};
struct SiteSelection {uint32_t siteIndex{},sourceDword{},shaderIndex{},descriptorOffset{};};
struct TableSelection {
    TableKey key;
    std::vector<uint32_t> offsets;
    uint32_t count{};
    std::vector<SlotInterval> uses;
};
struct Result {
    FamilyKey family;
    uint32_t evaluatedSites{};
    std::vector<SiteSelection> selections;
    std::vector<TableSelection> tables;
    bool selectorEvaluationComplete{},descriptorOwnershipProved{},inputContentsFrozen{};
    bool nativeExecutionPermission{},nativeExecutionPermitted{},performanceGainProved{};
    uint32_t nativeCommandsSuppressed{};
};
// Strict bounded package representation, not an independent original-IR audit.
// Failure clears output and never publishes a partial family/evaluation.
bool LoadCatalogJson(std::string_view,Catalog& output,std::string& error);
bool LoadCatalogFile(const std::filesystem::path&,Catalog& output,std::string& error);
bool Evaluate(const FamilyPlan&,std::span<const OwnedCbvBytes>,Result& output,std::string& error);
}
