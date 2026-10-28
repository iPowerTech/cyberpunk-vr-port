// VREngineStereoMenuShown() / VREngineStereoMenuClosed(): the main menu's controller was initialised / uninitialised
// (mods/redscript/CyberpunkVRPort_EngineStereo). Engine stereo starts at the first and stays on; both say whether the
// main menu is up (Stereo/EngineStereo.hpp NoteMainMenu, NoteMainMenuClosed). Outside engine stereo they do nothing.
#include "Natives/NativeFunctions.hpp"
#include "Stereo/EngineStereo.hpp"
void VREngineStereoMenuShown(RED4ext::IScriptable*,RED4ext::CStackFrame* f,void*,int64_t){++f->code;cvr::engine_stereo::NoteMainMenu();}
void VREngineStereoMenuClosed(RED4ext::IScriptable*,RED4ext::CStackFrame* f,void*,int64_t){++f->code;cvr::engine_stereo::NoteMainMenuClosed();}
