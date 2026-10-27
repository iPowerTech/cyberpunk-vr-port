#include "Core/GameSettings.hpp"
#include <windows.h>
#include <json/json.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace fs=std::filesystem;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Json::Value Parse(const std::string& text){
    Json::CharReaderBuilder builder;Json::Value value;std::string error;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Check(reader->parse(text.data(),text.data()+text.size(),&value,&error),"test JSON failed to parse");return value;
}
std::string Read(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Write(const fs::path& path,const std::string& text){std::ofstream f(path,std::ios::binary);f<<text;Check(bool(f),"fixture write failed");}
std::string Encode(const Json::Value& value){Json::StreamWriterBuilder writer;return Json::writeString(writer,value);}
Json::Value& Group(Json::Value& root,const std::string& name){
    for(auto& g:root["data"])if(g["group_name"]==name)return g;
    Json::Value g;g["group_name"]=name;g["options"]=Json::Value(Json::arrayValue);
    return root["data"].append(g);
}
Json::Value& Option(Json::Value& root,const std::string& group,const std::string& name){
    auto& options=Group(root,group)["options"];
    for(auto& option:options)if(option["name"]==name)return option;
    Json::Value option;option["name"]=name;option["value"]=0;return options.append(option);
}
bool Personal(const std::string& group,const std::string& name){
    return group=="/language" || group.starts_with("/audio/") || group=="/gameplay/difficulty" ||
        group.starts_with("/gallery/") || group=="/brightness" ||
        (group=="/gameplay/misc" && (name=="EnableTelemetry" || name=="EnableCloudSaves")) ||
        (group=="/video/display" && (name=="Gamma" || name=="PaperWhiteLevel" ||
                                     name=="MaxMonitorBrightness" || name=="TonemappingMidpoint"));
}
int main(int argc,char** argv)try {
    Check(argc==3,"preset and output directory required");
    auto preset=Parse(Read(fs::path(argv[1]))),player=preset;
    bool reflexFound=false;
    for(const auto& group:preset["data"])for(const auto& option:group["options"])
        if(option["name"]=="ReflexMode") {
            reflexFound=true;
            Check(option["value"]=="Disabled" && option["index"]==0,"shipped Reflex preset is not Off");
        }
    Check(reflexFound,"shipped Reflex preset missing");
    bool crosshairFound=false;
    for(const auto& group:preset["data"])for(const auto& option:group["options"]) {
        const auto groupName=group["group_name"].asString(),name=option["name"].asString();
        Check(groupName!="/brightness" && !(groupName=="/video/display" && Personal(groupName,name)),
              "shipped preset carries a picture calibration value");
        if(groupName=="/interface/hud" && name=="crosshairs") {
            crosshairFound=true;Check(option["value"]==false,"shipped HUD crosshair is not off");
        }
    }
    Check(crosshairFound,"shipped HUD crosshair setting missing");
    player["version"]=999;player["player_metadata"]="keep";
    unsigned expected=0;
    for(auto& group:player["data"])for(auto& option:group["options"]){
        const auto name=option["name"].asString(),groupName=group["group_name"].asString();
        if(option["value"].isBool())option["value"]=!option["value"].asBool();
        else if(option["value"].isNumeric())option["value"]=option["value"].asDouble()+123;
        else if(option["value"].isString())option["value"]="player_"+option["value"].asString();
        else option["value"]=Json::Value(Json::arrayValue);
        if(option.isMember("index"))option["index"]=999;
        option["player_schema_field"]=31;
        if(!Personal(groupName,name))++expected;
    }
    for(const char* name:{"OnScreen","Platform","Subtitles","VoiceOver"})Option(player,"/language",name)["value"]="ru";
    Option(player,"/gameplay/difficulty","GameDifficulty")["value"]="Story";
    Option(player,"/gameplay/misc","EnableTelemetry")["value"]=false;
    Option(player,"/gameplay/misc","EnableCloudSaves")["value"]=false;
    Option(player,"/gallery/favorites","GalleryFavorites")["value"]="my photos";
    Option(player,"/controls","future_binding")["value"]="keep binding";
    Option(player,"/future/version","Keep")["value"]="unknown group";
    const auto before=player;
    const auto merged=cvr::settings::MergeGameSettings(Encode(player),Encode(preset));
    Check(merged.ok && merged.changed==expected,"not every intended preset value was copied");
    auto result=Parse(merged.json);
    for(auto& group:preset["data"])for(auto& option:group["options"]){
        const auto path=group["group_name"].asString(),name=option["name"].asString();
        const auto& actual=Option(result,path,name);
        if(Personal(path,name))Check(actual==Option(player,path,name),"personal setting was altered");
        else {
            Check(actual["value"]==option["value"],"preset setting was not applied");
            if(option.isMember("index"))Check(actual["index"]==option["index"],"list selection index was not applied");
            Check(actual["player_schema_field"]==31,"player schema was replaced");
        }
    }
    Check(Group(result,"/language")==Group(player,"/language"),"language group disappeared");
    Check(result["version"]==999 && result["player_metadata"]=="keep","player metadata replaced");
    Check(Option(result,"/controls","future_binding")["value"]=="keep binding","unknown input setting lost");
    Check(Group(result,"/future/version")==Group(player,"/future/version"),"unknown group lost");
    Check(cvr::settings::MergeGameSettings(merged.json,Encode(preset)).changed==0,"merge is not idempotent");
    {
        // A preset that does carry them still leaves the player's brightness, gamma and HDR levels alone.
        auto calibrated=preset,own=player;
        const std::pair<const char*,const char*> picture[]={{"/brightness","Brightness"},{"/video/display","Gamma"},
            {"/video/display","PaperWhiteLevel"},{"/video/display","MaxMonitorBrightness"},{"/video/display","TonemappingMidpoint"}};
        for(const auto& [group,name]:picture){Option(calibrated,group,name)["value"]=1.0;Option(own,group,name)["value"]=77.5;}
        const auto kept=cvr::settings::MergeGameSettings(Encode(own),Encode(calibrated));
        Check(kept.ok,"calibration merge failed");
        auto keptJson=Parse(kept.json);
        for(const auto& [group,name]:picture)Check(Option(keptJson,group,name)["value"]==77.5,"player's brightness or gamma was overwritten");
    }
    const std::string empty=R"({"version":7,"data":[]})";
    const auto fresh=cvr::settings::MergeGameSettings(empty,Encode(preset));
    Check(fresh.ok,"missing preset groups could not be inserted");
    auto freshJson=Parse(fresh.json);
    for(const auto& group:freshJson["data"])for(const auto& option:group["options"])
        Check(!Personal(group["group_name"].asString(),option["name"].asString()),"personal defaults inserted into absent player group");
    for(const auto& bad:{std::string("{"),std::string("[]"),std::string(R"({"data":[],"data":[]})"),
        std::string(R"({"data":[{"group_name":"/controls","options":[{"name":"x","value":1},{"name":"x","value":2}]}]})")}){
        Check(!cvr::settings::MergeGameSettings(bad,Encode(preset)).ok,"invalid player settings accepted");
        Check(!cvr::settings::MergeGameSettings(Encode(player),bad).ok,"invalid preset accepted");
    }
    const auto directory=fs::path(argv[2])/(L"settings-\u0416-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directory(directory);
    const auto userFile=directory/L"UserSettings.json",sourceFile=directory/L"preset.json";
    const auto original=Encode(before);Write(userFile,original);Write(sourceFile,Encode(preset));
    const auto installed=cvr::settings::InstallGameSettings(sourceFile,userFile);
    Check(installed.ok && installed.changed==expected && !installed.backup.empty(),"first install failed");
    Check(Read(installed.backup)==original,"backup did not preserve exact original bytes");
    Check(Parse(Read(userFile))==Parse(merged.json),"installed result differs from merge");
    const auto again=cvr::settings::InstallGameSettings(sourceFile,userFile);
    Check(again.ok && again.changed==0 && again.backup.empty(),"matching file was unnecessarily rewritten");
    Write(userFile,"{ broken");const auto invalid=Read(userFile);
    Check(!cvr::settings::InstallGameSettings(sourceFile,userFile).ok && Read(userFile)==invalid,"parse failure changed player's file");
    Write(userFile,original);Check(SetFileAttributesW(userFile.c_str(),FILE_ATTRIBUTE_READONLY),"readonly fixture failed");
    const auto locked=cvr::settings::InstallGameSettings(sourceFile,userFile);
    Check(!locked.ok && Read(userFile)==original,"failed replacement changed original file");
    SetFileAttributesW(userFile.c_str(),FILE_ATTRIBUTE_NORMAL);
    for(const auto& file:fs::directory_iterator(directory))Check(file.path().extension()!=L".tmp","temporary file was not cleaned up");
    std::cout<<"PASS "<<expected<<" preset values, personal exclusions, unknown entries, metadata, strict JSON, Unicode path, backup, atomic failure and repeat\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
