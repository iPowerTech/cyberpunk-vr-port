// CyberpunkVRPort -- NO AUTOMATIC RELOAD. Folded into this mod on 2026-08-20; everything below the
// second banner is the original file, byte for byte.
//
// WHY THE PORT NEEDS IT. It replaces NoAmmoDecisions.ToReload so that emptying a magazine never starts
// a reload by itself -- the player has to press Reload. The port's reload is driven by hand: the
// magazine is dropped with B, a fresh one is brought up and seated, the slide is racked with the right
// stick click. An automatic reload races all of that and wins, and the hands are then animating a
// magazine the game has already replaced.
//
// WHERE IT CAME FROM. It was a loose file in r6\scripts (dated 2021-10-11) that the port silently
// depended on -- so a tester without it got a reload that fires itself. It lives here now so it ships
// and is versioned with the port instead of depending on whatever happens to sit in r6\scripts. The
// loose copy was DELETED in the same change: two @replaceMethod on one method is a hard redscript
// error, not a merge.
//
// WHAT IT TOUCHES, and it collides with nothing of ours: @replaceMethod(NoAmmoDecisions).ToReload, and
// no other file in this port mentions NoAmmoDecisions.
//
// ONE BEHAVIOUR WORTH KNOWING, because it is not obvious from the name: the switch below decides what
// pressing FIRE on an empty weapon does. For the fifteen weapons named there, nothing at all. For
// every other weapon it plays a dry-fire click. Five of the named ones -- yukimura, chao, kenshin,
// lexington, omaha -- are weapons the port's physical reload also drives, so if a dry-fire sound is
// wanted on those, that list is the place.
//
// 2.5 ADDED TWO REFUSALS to the game's own ToReload, and a replacement drops whatever the game adds: no
// reload while the shoot state is 4, 5 or 6, and none on the frame QuickMelee is pressed. Both are merged
// below, marked "2.5", at the places the game checks them; the rest of the original is unchanged.
//
// ============================== ORIGINAL FILE FOLLOWS ==============================

@replaceMethod(NoAmmoDecisions)
  protected final const func ToReload(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
    let weaponObject: ref<WeaponObject> = this.GetWeaponObject(scriptInterface);
	let weaponData: wref<gameItemData> = weaponObject.GetItemData();
	let weaponName: String = NameToString(weaponData.GetName());
    // 2.5
    let shootState: Int32 = scriptInterface.localBlackboard.GetInt(GetAllBlackboardDefs().PlayerStateMachine.ShootState);
    if !WeaponObject.CanReload(weaponObject) {
      return false;
    };
    // 2.5
    if shootState == 4 || shootState == 5 || shootState == 6 {
      return false;
    };
    if !scriptInterface.HasStatFlag(gamedataStatType.CanWeaponReloadWhileSprinting) && scriptInterface.localBlackboard.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Locomotion) == EnumInt(gamePSMLocomotionStates.Sprint) {
      return false;
    };
    if !scriptInterface.HasStatFlag(gamedataStatType.CanWeaponReloadWhileVaulting) && stateContext.IsStateActive(n"Locomotion", n"vault") {
      return false;
    };
    if !scriptInterface.HasStatFlag(gamedataStatType.CanWeaponReloadWhileSliding) && stateContext.IsStateActive(n"Locomotion", n"slide") {
      return false;
    };
    // 2.5
    if scriptInterface.IsActionJustPressed(n"QuickMelee") {
      return false;
    };
    if scriptInterface.IsActionJustPressed(n"RangedAttack") {
		switch(weaponName){
			case "w_submachinegun_arasaka_shingen":
			case "w_special_kangtao_dian":
			case "w_handgun_arasaka_yukimura":
			case "w_handgun_kangtao_chao":
			case "w_rifle_precision_militech_achilles":
			case "w_2020_shotgun_blunderbuss":
			case "w_shotgun_zhuo":
			case "w_rifle_sniper_tsunami_ashura":
			case "w_rifle_assault_nokota_sidewinder":
			case "w_rifle_sniper_tsunami_nekomata":
			case "w_revolver_darra_quasar":
			case "w_revolver_techtronika_burya":
			case "w_handgun_arasaka_kenshin":
			case "w_handgun_militech_lexington":
			case "w_handgun_militech_omaha":
				return false;
			default:
				GameObject.PlaySound(weaponObject, n"w_gun_shotgun_power_tactician_dry_fire");
				return false;
		}
    };
    //if Equals(weaponObject.GetWeaponRecord().ItemType().Type(), gamedataItemType.Wea_SniperRifle) {
    //  return !stateContext.IsStateActive(n"UpperBody", n"aimingState");
    //};
	if scriptInterface.IsActionJustPressed(n"Reload"){
		return true;
	}
    return false;
}