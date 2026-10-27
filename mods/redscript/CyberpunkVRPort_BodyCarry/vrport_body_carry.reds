// CyberpunkVRPort -- how the carry animation holds a body, reported to the plugin.
//
// A carried body is held by one arm or by both, and only the carry state machine knows which: it writes
// AnimFeature_Carry.useBothHands (both for an ordinary carry; the left arm alone for the strong carry
// with a firearm out, and for the wounded-soldier carry). VRIK hands that many arms to the animation
// (src/Hooks/AnimPose.cpp), so the plugin is told after every write.
//
// This file intentionally stays in the global namespace: the plugin registers plain global natives.

native func SetVRBodyCarryBothHands(bothHands: Bool) -> Int32;

@wrapMethod(CarriedObjectEvents)
protected final func SetAnimFeature_Carry(state: ECarryState, instant: Bool, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>, opt setExecutionOwnerOnly: Bool) -> Void {
  wrappedMethod(state, instant, stateContext, scriptInterface, setExecutionOwnerOnly);
  if IsDefined(this.m_animCarryFeature) {
    SetVRBodyCarryBothHands(this.m_animCarryFeature.useBothHands);
  };
}
