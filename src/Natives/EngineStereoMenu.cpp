// VREngineStereoMenuShown() / VREngineStereoMenuClosed(): the main menu's controller was initialised / uninitialised
// (mods/redscript/CyberpunkVRPort_EngineStereo). Engine stereo starts at the first and stays on; both say whether the
// main menu is up (Stereo/EngineStereo.hpp NoteMainMenu, NoteMainMenuClosed). Outside engine stereo they do nothing.
#include "Natives/NativeFunctions.hpp"
#include "Stereo/EngineStereo.hpp"
extern void Log(const char* fmt, ...);
void VREngineStereoMenuShown(RED4ext::IScriptable*,RED4ext::CStackFrame* f,void*,int64_t){++f->code;cvr::engine_stereo::NoteMainMenu();}
void VREngineStereoMenuClosed(RED4ext::IScriptable*,RED4ext::CStackFrame* f,void*,int64_t){++f->code;cvr::engine_stereo::NoteMainMenuClosed();}
// DIAGNOSTIC (2026-10-28): the script's menu scenarios and player attach/detach, into the port log beside the
// capture's own decisions (EngineStereo.cpp LogCaptureDecision).
void VREngineStereoTrace(RED4ext::IScriptable*,RED4ext::CStackFrame* f,void*,int64_t){
    RED4ext::CString text;RED4ext::GetParameter(f,&text);++f->code;
    Log("[engine-stereo-trace] %s\n",text.c_str());
}
