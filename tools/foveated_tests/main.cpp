#include "Render/FoveatedDlss.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cvr::foveated;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() try {
    Check(GetSettings().mode == 0, "must default off");
    Check(GetSettings() == Settings{0,70,65,false,0,-10},"default region must be 70 x 65 percent with Y=-10 percent");
    for (int mode = 0; mode <= 3; ++mode) for (float width : {20.0f, 60.0f, 100.0f}) for (float height : {30.0f, 70.0f, 100.0f}) {
        const auto value=Normalize({mode,width,height,true});
        Check(Enabled(value)==(mode!=0),"off bypass");
        Check(value.widthPercent==width && value.heightPercent==height,"independent region dimensions");
    }
    Check(Normalize({9,190,190,false}) == Settings{3,100,100,false,0,0}, "upper clamp");
    Check(Normalize({-1,-10,-10,false}) == Settings{0,20,20,false}, "lower clamp");
    Check(Normalize({1,NAN,NAN,false,NAN,NAN}) == Settings{1,70,65,false,0,-10}, "invalid values must use current defaults");
    Check(Normalize({1,60,80,false,40,-40}) == Settings{1,60,80,false,20,-10},"offset must keep the region in bounds");
    Check(Normalize({1,100,100,false,30,30}) == Settings{1,100,100,false,0,0},"full-frame offsets must be zero");
    Check(Normalize({1,60,100,false,INFINITY,NAN}).offsetYPercent == 0,"invalid offset fallback must be clamped to the image");
    const auto center=CenterRegion({1,60,60,false,0,0},1485,1485,2560,2560);
    Check(center==Region{512,512,1536,1536,891,891,297,297},"center alignment changed");
    const auto lowered=CenterRegion({1,60,60,false,-10,20},1485,1485,2560,2560);
    Check(lowered.sourceX < center.sourceX && lowered.sourceY > center.sourceY,"offset directions");
    Check(lowered.y+lowered.height == 2560 && lowered.sourceY+lowered.inputH == 1485,"lowered region must reach the lower edge exactly");
    for(float offset:{-80.0f,0.0f,80.0f})for(float width:{20.0f,69.3f,100.0f}) {
        const auto r=CenterRegion({1,width,43.7f,false,offset,-offset},1485,1279,2560,2208);
        Check(r.sourceX+r.inputW<=1485 && r.sourceY+r.inputH<=1279 && r.x+r.width<=2560 && r.y+r.height<=2208,"shifted crop outside the image");
    }
    SettingsDelay delay;
    Settings first{2,60,60},second{2,70,40,false,10,20};
    delay.Request(first,100);delay.Request(first,1050);delay.Update(1099);Check(delay.Applied().mode==0,"applied before one second");
    delay.Update(1100);Check(delay.Applied()==first,"identical INI polling restarted the timer");
    delay.SetEditing(true,1200);delay.Request(second,1200);delay.Update(12000);
    Check(delay.Applied()==first && delay.Requested()==second,"held slider applied while stationary");
    delay.SetEditing(false,12000);delay.Update(12999);Check(delay.Applied()==first,"release delay was too short");
    delay.Update(13000);Check(delay.Applied()==second,"final slider value was not applied one second after release");
    const Settings third{3,50,50};
    delay.Request(first,14000);delay.Request(third,14500);delay.Update(15000);
    Check(delay.Applied()==second,"intermediate drag value was applied");
    delay.Update(15500);Check(delay.Applied()==third,"last drag value was not applied");
    delay.SetEditing(true,15500);delay.Request({0,60,60},15500);
    Check(delay.Applied().mode==0,"Off must apply immediately even while editing");
    delay.SetEditing(false,15500);delay.Request(first,15600);delay.Update(16600);
    Check(delay.Applied()==first,"panel closure left settings stuck in editing");
    Settings loaded;
    Check(ParseSetting("xr_foveated_edges=nan\n", loaded) && loaded.widthPercent==70 && loaded.heightPercent==65,"invalid legacy inset must use current defaults");
    Check(ParseSetting("xr_foveated_dlss = 2\n", loaded), "spaced mode");
    Check(ParseSetting("xr_foveated_edges=25.5\n", loaded), "edge input");
    Check(loaded.widthPercent == 49 && loaded.heightPercent == 49, "legacy edge migration");
    Check(ParseSetting("xr_foveated_width=70\n", loaded), "width input");
    Check(ParseSetting("xr_foveated_height=40\n", loaded), "height input");
    Check(ParseSetting("xr_foveated_edges=20\n", loaded), "legacy key after dimensions");
    Check(loaded.widthPercent == 70 && loaded.heightPercent == 40, "legacy key overwrote explicit dimensions");
    Check(ParseSetting("xr_foveated_boundary=1\n", loaded), "boundary input");
    Check(ParseSetting("xr_foveated_offset_x=-10\n", loaded), "horizontal offset input");
    Check(ParseSetting("xr_foveated_offset_y=20\n", loaded), "vertical offset input");
    Check(!ParseSetting("xr_foveated_edges_extra=33\n", loaded), "unrelated setting matched");
    SetSettings(loaded); Check(GetSettings() == Settings{2,70,40,true,-10,20}, "requested settings state");
    FILE* file{}; Check(tmpfile_s(&file)==0 && file, "temporary file"); WriteSettings(file,loaded); std::rewind(file);
    Settings restored; char line[128]; while (std::fgets(line,sizeof(line),file)) Check(ParseSetting(line,restored), "round trip parse");
    std::fclose(file); Check(restored == loaded, "settings round trip");
    std::cout << "PASS shifted region bounds, one-second debounce, stationary hold/release, immediate Off and INI round trip\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
