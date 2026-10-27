// CyberpunkVRPort -- the hits the player's guard takes, felt in the hands.
//
// DamageManager.ProcessDefensiveState (inside DamageSystem.ModifyHitData) is where the game decides
// what a hit on a guarding player became, and it writes the answer into the hit's flags:
//   WasBulletDeflected / WasBulletParried   a bullet sent back off the blade
//   WasBulletBlocked (with WasBlocked)      a bullet stopped by it
//   WasDeflected                            a melee hit parried
//   WasBlocked                              a melee hit taken on the block
// So the flags are read after it runs, on the real pipeline only (projectionPipeline is the damage
// preview), and the plugin plays the matching vibration (src/Runtimes/Haptics.cpp).
//
// WHICH HANDS: the weapon hand (right); the plugin adds the left when the weapon is in a two-handed
// grip. Fists, gorilla arms and mantis blades guard with both arms, so they always ask for both.
//
// This file intentionally stays in the global namespace: the plugin registers plain global natives.

native func VRHapticEvent(kind: Int32, hands: Int32) -> Void;

func VRPortGuardHands(player: ref<PlayerPuppet>) -> Int32 {
  let weapon: ref<WeaponObject> = GameInstance.GetTransactionSystem(player.GetGame()).GetItemInSlot(player, t"AttachmentSlots.WeaponRight") as WeaponObject;
  if IsDefined(weapon) {
    let id: ItemID = weapon.GetItemID();
    if WeaponObject.IsOfType(id, gamedataItemType.Wea_Fists) || WeaponObject.IsOfType(id, gamedataItemType.Cyb_StrongArms)
      || WeaponObject.IsOfType(id, gamedataItemType.Cyb_MantisBlades) {
      return 3;
    };
  };
  return 2;
}

@wrapMethod(DamageSystem)
private func ModifyHitData(hitEvent: ref<gameHitEvent>) -> Void {
  wrappedMethod(hitEvent);
  if hitEvent.projectionPipeline || !IsDefined(hitEvent.attackData) {
    return;
  };
  let player: ref<PlayerPuppet> = hitEvent.target as PlayerPuppet;
  if !IsDefined(player) {
    return;
  };
  let data: ref<AttackData> = hitEvent.attackData;
  let kind: Int32 = 0;
  if data.WasBulletDeflected() {
    kind = 13;
  } else {
    if data.WasBulletBlocked() {
      kind = 12;
    } else {
      if data.WasDeflected() {
        kind = 11;
      } else {
        if data.WasBlocked() {
          kind = 10;
        };
      };
    };
  };
  if kind != 0 {
    VRHapticEvent(kind, VRPortGuardHands(player));
  };
}
