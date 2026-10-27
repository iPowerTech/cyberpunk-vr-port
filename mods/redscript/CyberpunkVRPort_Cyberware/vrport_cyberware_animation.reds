public native func VRCyberwareMusclePulse(side: Int32, duration: Float) -> Void;

@addMethod(PlayerPuppet)
public func VRCyberwareAnimateSwing(weapon: wref<WeaponObject>, record: wref<Attack_Melee_Record>, side: Int32, strong: Bool) -> Void {
  let speed = MaxF(GameInstance.GetStatsSystem(this.GetGame()).GetStatValue(Cast<StatsObjectID>(weapon.GetEntityID()), gamedataStatType.AttackSpeed), 0.10);
  let duration = ClampF((record.StartupDuration() + record.ActiveDuration() + record.RecoverDuration()) / speed, 0.12, 1.50);
  VRCyberwareMusclePulse(side, duration);
}
