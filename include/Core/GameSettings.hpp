#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace cvr::settings {
struct MergeResult {bool ok{};unsigned changed{};std::string json,error;};
struct InstallResult {bool ok{};unsigned changed{};std::filesystem::path backup;std::string error;};
MergeResult MergeGameSettings(std::string_view player,std::string_view preset);
InstallResult InstallGameSettings(const std::filesystem::path& preset,const std::filesystem::path& player);
}
