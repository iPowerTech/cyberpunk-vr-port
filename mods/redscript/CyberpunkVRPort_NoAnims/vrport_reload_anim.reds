// CyberpunkVRPort -- the native reload, reported to the plugin.
//
// Pressing Reload plays the game's own reload on the arms. In VRIK the plugin solves both arms to the
// controllers over it, so it has to be told when the reload runs: both arms then ease onto the
// animation and back (src/Anim/ReloadHandOff.cpp).
//
// Entered with the seconds until the state can end -- its logical duration plus the end time
// IsReloadDurationComplete waits for -- so the hands can start back before it does. A shell of a
// looped reload (ReloadAmount > 0: shotguns, revolvers loading round by round) that does not fill the
// magazine is followed by the next shell, so its end is passed as unknown (-1) and the hands stay
// until the state really exits.
//
// This file intentionally stays in the global namespace: the plugin registers plain global natives.

native func SetVRReloadAnimation(active: Bool, finishIn: Float) -> Int32;

func VRPortReloadFinishIn(events: ref<ReloadEvents>, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Float {
  let logical: StateResultFloat = stateContext.GetPermanentFloatParameter(n"ReloadLogicalDuration");
  if !logical.valid {
    return -1.00;
  };
  let weapon: ref<WeaponObject> = scriptInterface.owner as WeaponObject;
  let amount: Int32 = IsDefined(events.m_animReloadData) ? events.m_animReloadData.amountToReload : 0;
  if amount > 0 && IsDefined(weapon)
    && Cast<Int32>(WeaponObject.GetMagazineAmmoCount(weapon)) + amount < Cast<Int32>(WeaponObject.GetMagazineCapacity(weapon)) {
    return -1.00;
  };
  let endStat: gamedataStatType = stateContext.GetBoolParameter(n"EmptyReload", true) ? gamedataStatType.EmptyReloadEndTime : gamedataStatType.ReloadEndTime;
  let endTime: Float = scriptInterface.GetStatsSystem().GetStatValue(Cast<StatsObjectID>(scriptInterface.ownerEntityID), endStat);
  return logical.value + MaxF(endTime, 0.00);
}

@wrapMethod(ReloadEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  SetVRReloadAnimation(true, VRPortReloadFinishIn(this, stateContext, scriptInterface));
}

@wrapMethod(ReloadEvents)
protected func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  SetVRReloadAnimation(false, 0.00);
}

@wrapMethod(ReloadEvents)
protected func OnForcedExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  SetVRReloadAnimation(false, 0.00);
}
