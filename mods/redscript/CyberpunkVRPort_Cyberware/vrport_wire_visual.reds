@addField(PlayerPuppet) private let vrWireEffectWeapon: wref<WeaponObject>;
@addField(PlayerPuppet) private let vrWireEffectActive: Bool;

@addMethod(PlayerPuppet)
public func VRCyberwareWireEffect(weapon: wref<WeaponObject>, active: Bool) -> Void {
  if this.vrWireEffectWeapon != weapon {
    if this.vrWireEffectActive && IsDefined(this.vrWireEffectWeapon) {
      GameObjectEffectHelper.StopEffectEvent(this.vrWireEffectWeapon, n"monowire_idle");
    };
    this.vrWireEffectWeapon = weapon;
    this.vrWireEffectActive = false;
  };
  if !IsDefined(weapon) || this.VRCyberwareKind(weapon) != 3 { return; };
  if NotEquals(active, this.vrWireEffectActive) {
    if active { GameObjectEffectHelper.StartEffectEvent(weapon, n"monowire_idle"); }
    else { GameObjectEffectHelper.StopEffectEvent(weapon, n"monowire_idle"); };
    this.vrWireEffectActive = active;
  };
}
