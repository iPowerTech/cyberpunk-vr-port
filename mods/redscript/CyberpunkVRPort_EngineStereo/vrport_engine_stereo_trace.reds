// CyberpunkVRPort — DIAGNOSTIC (2026-10-28): which menu scenario is up and when the player attaches, in the port log
// beside the capture's own split/squeeze decisions ([capture-decision], src/Stereo/EngineStereo.cpp). Under engine
// stereo the pre-game screens and the first part of a load out of the main menu were split into the eyes, three times
// the size, while the same screens from the pause menu were right; this says what tells the two apart.

native func VREngineStereoTrace(text: String) -> Void;

@wrapMethod(MenuScenario_PreGameSubMenu)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  VREngineStereoTrace("enter " + NameToString(this.GetClassName()) + " from " + NameToString(prevScenario));
  return wrappedMethod(prevScenario, userData);
}

@wrapMethod(MenuScenario_PreGameSubMenu)
protected cb func OnLeaveScenario(nextScenario: CName) -> Bool {
  VREngineStereoTrace("leave " + NameToString(this.GetClassName()) + " to " + NameToString(nextScenario));
  return wrappedMethod(nextScenario);
}

@wrapMethod(MenuScenario_SingleplayerMenu)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  VREngineStereoTrace("enter MenuScenario_SingleplayerMenu (own) from " + NameToString(prevScenario));
  return wrappedMethod(prevScenario, userData);
}

@wrapMethod(MenuScenario_SingleplayerMenu)
protected cb func OnLeaveScenario(nextScenario: CName) -> Bool {
  VREngineStereoTrace("leave MenuScenario_SingleplayerMenu (own) to " + NameToString(nextScenario));
  return wrappedMethod(nextScenario);
}

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
  VREngineStereoTrace("player attached");
  return wrappedMethod();
}

@wrapMethod(PlayerPuppet)
protected cb func OnDetach() -> Bool {
  VREngineStereoTrace("player detached");
  return wrappedMethod();
}
