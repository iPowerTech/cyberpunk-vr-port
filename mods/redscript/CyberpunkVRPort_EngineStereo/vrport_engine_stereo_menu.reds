// CyberpunkVRPort — ENGINE STEREO STARTS AT THE MAIN MENU.
//
// In engine stereo (xr_engine_stereo=1) the plugin keeps the engine mono until the main menu is first shown: splash
// screens, intro videos and the first loading are one picture in the headset. The main menu's controller tells the
// plugin when it is up; from then on the engine renders both eyes (src/Stereo/EngineStereoSdk.cpp). It also says when
// the main menu goes: under it the loading layer stays active, but only a loading screen without the main menu is
// drawn side by side (src/Stereo/EngineStereo.cpp LoadingSideBySide). Outside engine stereo the calls do nothing.

native func VREngineStereoMenuShown() -> Void;
native func VREngineStereoMenuClosed() -> Void;

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
  let result: Bool = wrappedMethod();
  VREngineStereoMenuShown();
  return result;
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnUninitialize() -> Bool {
  VREngineStereoMenuClosed();
  return wrappedMethod();
}
