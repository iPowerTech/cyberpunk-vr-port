#pragma once
#include "Overlay/VrInteraction.hpp"
#include <string>
#include <vector>

namespace cvr::vrui {
Settings GetSettings();
void SetSettings(Settings);
bool ParseSetting(const char*,Settings&);
void WriteSettings(void*,const Settings&);
bool Visible();
bool CapturesInput();
void Toggle();
// L3 + R3 held half a second: the VR menu (mods/cet/CyberpunkVRPort_Menu) is asked to open or close, once
// per hold; the panel itself is F10 / INSERT. Read (and cleared) by VRMenuGesture.
bool ConsumeMenuSummon();
void SelectMenuItem(unsigned);
void Close(bool resume=true);
void Recenter();
bool ConsumePlacementSave();
void UpdateTracking(const Tracking&);
int PollCommand(); // 1 open panel, 2 close panel, 100+ selected game action
void BridgeUpdate(bool ready,bool inGame,const char* labels);
std::vector<std::string> MenuItems();
struct PointerEvent {float x=-1,y=-1,wheel=0;bool down=false;};
std::vector<PointerEvent> ConsumePointerEvents();
struct View {XrPosef pose{{0,0,0,1},{}};XrVector3f rayStart{},rayEnd{},headPosition{};float width{},height{},hold{};Hit hit{};bool valid=false,ray=false,dragging=false;};
View GetView();
}
