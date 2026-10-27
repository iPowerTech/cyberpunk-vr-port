// The popups' visual state (inkModalPopupState) lays one mono picture over both eyes in VR, so the
// plugin decides whether the request and its matching restore reach the game. See
// VRPopupVisualState in src/Natives/UiPopup.cpp.
native func VRPopupVisualState(state: CName, request: Bool) -> Bool;

@wrapMethod(UISystem)
public final func RequestNewVisualState(newVisualState: CName) -> Void {
  if VRPopupVisualState(newVisualState, true) {
    wrappedMethod(newVisualState);
  }
}

@wrapMethod(UISystem)
public final func RestorePreviousVisualState(popVisualState: CName) -> Void {
  if VRPopupVisualState(popVisualState, false) {
    wrappedMethod(popVisualState);
  }
}
