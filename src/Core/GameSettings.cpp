#include "Core/GameSettings.hpp"
#include <windows.h>
#include <json/json.h>
#include <fstream>
#include <memory>
#include <set>
#include <unordered_map>

namespace cvr::settings {
namespace {
bool InGroup(std::string_view group,std::string_view root) {
    return group==root || (group.starts_with(root) && group.size()>root.size() && group[root.size()]=='/');
}
// The player's own: language, sound, difficulty, gallery, telemetry and cloud saves, and the picture
// calibration -- brightness, gamma and the HDR brightness levels are set for the player's eyes and
// display, so the VR preset never carries or overwrites them (the HDR mode itself stays in it).
bool Preserve(std::string_view group,std::string_view name) {
    return InGroup(group,"/language") || InGroup(group,"/audio") ||
        InGroup(group,"/gameplay/difficulty") || InGroup(group,"/gallery") || InGroup(group,"/brightness") ||
        (group=="/gameplay/misc" && (name=="EnableTelemetry" || name=="EnableCloudSaves")) ||
        (group=="/video/display" && (name=="Gamma" || name=="PaperWhiteLevel" ||
                                     name=="MaxMonitorBrightness" || name=="TonemappingMidpoint"));
}
bool Parse(std::string_view text,Json::Value& root,std::string& error) {
    Json::CharReaderBuilder builder;Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["collectComments"]=false;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if(!reader->parse(text.data(),text.data()+text.size(),&root,&error))return false;
    if(!root.isObject() || !root["data"].isArray()){error="Expected a settings object with a data array";return false;}
    std::set<std::string> groups;
    for(const auto& group:root["data"]) {
        if(!group.isObject() || !group["group_name"].isString() || group["group_name"].asString().empty() ||
           !group["options"].isArray()){error="Invalid settings group";return false;}
        const auto name=group["group_name"].asString();
        if(!groups.insert(name).second){error="Duplicate settings group: "+name;return false;}
        std::set<std::string> options;
        for(const auto& option:group["options"]) {
            if(!option.isObject() || !option["name"].isString() || option["name"].asString().empty() || !option.isMember("value")) {
                error="Invalid setting in "+name;return false;
            }
            if(!options.insert(option["name"].asString()).second){error="Duplicate setting in "+name;return false;}
        }
    }
    return true;
}
bool Read(const std::filesystem::path& path,std::string& text) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)return false;
    const auto size=file.tellg();
    if(size<=0 || size>16*1024*1024)return false;
    text.resize(static_cast<size_t>(size));file.seekg(0);
    return static_cast<bool>(file.read(text.data(),static_cast<std::streamsize>(size)));
}
std::string WinError(const char* action) {return std::string(action)+" (Windows error "+std::to_string(GetLastError())+")";}
}
MergeResult MergeGameSettings(std::string_view player,std::string_view preset) {
    MergeResult result;Json::Value current,shipped;
    if(!Parse(player,current,result.error)){result.error="Player settings: "+result.error;return result;}
    if(!Parse(preset,shipped,result.error)){result.error="VR preset: "+result.error;return result;}
    auto& groups=current["data"];
    std::unordered_map<std::string,Json::ArrayIndex> byGroup;
    for(Json::ArrayIndex i=0;i<groups.size();++i)byGroup.emplace(groups[i]["group_name"].asString(),i);
    for(const auto& sourceGroup:shipped["data"]) {
        const auto groupName=sourceGroup["group_name"].asString();
        if(Preserve(groupName,{}))continue;
        bool copyGroup=false;
        for(const auto& option:sourceGroup["options"])copyGroup|=!Preserve(groupName,option["name"].asString());
        if(!copyGroup)continue;
        auto found=byGroup.find(groupName);
        if(found==byGroup.end()) {
            auto newGroup=sourceGroup;newGroup["options"]=Json::Value(Json::arrayValue);
            found=byGroup.emplace(groupName,groups.size()).first;groups.append(std::move(newGroup));
        }
        auto& options=groups[found->second]["options"];
        std::unordered_map<std::string,Json::ArrayIndex> byName;
        for(Json::ArrayIndex i=0;i<options.size();++i)byName.emplace(options[i]["name"].asString(),i);
        for(const auto& source:sourceGroup["options"]) {
            const auto name=source["name"].asString();
            if(Preserve(groupName,name))continue;
            const auto option=byName.find(name);
            if(option==byName.end()) {options.append(source);++result.changed;continue;}
            auto& target=options[option->second];
            if(target["type"].isString() && source["type"].isString() && target["type"]!=source["type"]) {
                result.error="Incompatible setting type: "+groupName+"/"+name;return result;
            }
            // Keep the player's schema/defaults and unknown fields. Only the
            // saved value and its list index belong to the first-launch preset.
            bool changed=target["value"]!=source["value"];
            target["value"]=source["value"];
            if(source.isMember("index")) {
                changed|=target["index"]!=source["index"];target["index"]=source["index"];
            }
            if(changed)++result.changed;
        }
    }
    Json::StreamWriterBuilder writer;writer["indentation"]="    ";
    result.json=Json::writeString(writer,current)+"\n";result.ok=true;return result;
}
InstallResult InstallGameSettings(const std::filesystem::path& preset,const std::filesystem::path& player) {
    InstallResult result;std::string source,existing;
    if(!Read(preset,source)){result.error="Cannot read VR preset";return result;}
    if(!Read(player,existing)){result.error="Cannot read existing player settings";return result;}
    const auto merged=MergeGameSettings(existing,source);
    if(!merged.ok){result.error=merged.error;return result;}
    result.changed=merged.changed;
    if(!merged.changed){result.ok=true;return result;}
    SYSTEMTIME time{};GetLocalTime(&time);
    wchar_t suffix[96]{};
    swprintf_s(suffix,L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",time.wYear,time.wMonth,time.wDay,
               time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId());
    const auto backup=player.parent_path()/(L"UserSettings.pre-vr-"+std::wstring(suffix)+L".json");
    const auto temporary=player.parent_path()/(L"UserSettings.vrport-"+std::wstring(suffix)+L".tmp");
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){result.error=WinError("Cannot create temporary settings");return result;}
    DWORD written{};
    const bool saved=WriteFile(file,merged.json.data(),static_cast<DWORD>(merged.json.size()),&written,nullptr) &&
        written==merged.json.size() && FlushFileBuffers(file);
    if(!saved)result.error=WinError("Cannot write temporary settings");
    CloseHandle(file);
    if(saved) {
        if(!CopyFileW(player.c_str(),backup.c_str(),TRUE))result.error=WinError("Cannot back up player settings");
        else {
            result.backup=backup;
            if(MoveFileExW(temporary.c_str(),player.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))result.ok=true;
            else result.error=WinError("Cannot install merged settings");
        }
    }
    if(!result.ok)DeleteFileW(temporary.c_str());
    return result;
}
}
