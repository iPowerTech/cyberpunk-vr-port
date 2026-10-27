// Register the state as well as its edges during TweakXL's application phase.
// The native PSM loader enumerates Definitions.<machine>.states; transition
// targets alone do not register a state (native loader RVA 0x674E5C).
public class VrLauncherStateTweak extends ScriptableTweak {
  protected func OnApply() -> Void {
    let root = "playerStateMachineLeftHandCyberware.";
    let dst = root + "vrLauncherReady.";
    let src = root + "leftHandCyberwareWaitForUnequip.";
    let fields: array<String> = ["stateBodyClassName", "affinity", "alias", "forceInputContext", "transitionConditionOverridesEnterCondition", "transitionSkipInternalCondition"];
    let field: String;
    let state: String;
    let states: array<String> = ["leftHandCyberwareEquip", "leftHandCyberwareQuickAction", "leftHandCyberwareChargeAction", "leftHandCyberwareChargeRepeatAction", "leftHandCyberwareWaitForUnequip"];
    let registered = TweakDBInterface.GetStringArray(t"playerStateMachineDefinitions.LeftHandCyberware.states");
    let readyTargets: array<String> = ["leftHandCyberwareUnequip", "leftHandCyberwareCharge", "leftHandCyberwareQuickAction"];
    let readyConditions: array<String> = ["=", "=", "="];
    if ArraySize(registered) == 0 { return; };
    // Use the package's local state name. A qualified entry resolves OnEnter
    // but does not match the native outgoing-transition lookup.
    if !ArrayContains(registered, "vrLauncherReady") {
      ArrayPush(registered, "vrLauncherReady");
      TweakDBManager.SetFlat(t"playerStateMachineDefinitions.LeftHandCyberware.states", registered);
    };
    for field in fields {
      TweakDBManager.SetFlat(TDBID.Create(dst + field), TweakDBInterface.GetFlat(TDBID.Create(src + field)));
    };
    TweakDBManager.SetFlat(TDBID.Create(dst + "hasEnterCondition"), true);
    TweakDBManager.SetFlat(TDBID.Create(dst + "hasExitCondition"), false);
    TweakDBManager.SetFlat(TDBID.Create(dst + "hasOnEnter"), true);
    TweakDBManager.SetFlat(TDBID.Create(dst + "hasOnExit"), true);
    TweakDBManager.SetFlat(TDBID.Create(dst + "hasOnUpdate"), true);
    // Explicit types are required at the Variant boundary. An untyped literal
    // is inferred as array<CName>; the native PSM reads these as array<String>.
    TweakDBManager.SetFlat(TDBID.Create(dst + "transitionTo"), readyTargets);
    TweakDBManager.SetFlat(TDBID.Create(dst + "transitionCondition"), readyConditions);
    for state in states { this.AddReadyEdge(root + state + "."); };
  }

  private func AddReadyEdge(prefix: String) -> Void {
    let strings = TweakDBInterface.GetStringArray(TDBID.Create(prefix + "transitionTo"));
    let conditions = TweakDBInterface.GetStringArray(TDBID.Create(prefix + "transitionCondition"));
    let flags: array<Bool>;
    let field: String;
    let fields: array<String> = ["transitionConditionOverridesEnterCondition", "transitionSkipInternalCondition"];
    // Preserve incompatible definitions instead of guessing another type.
    if ArraySize(strings) == 0 || ArraySize(strings) != ArraySize(conditions) || ArrayContains(strings, "vrLauncherReady") { return; };
    ArrayInsert(strings, 0, "vrLauncherReady");
    ArrayInsert(conditions, 0, "=");
    TweakDBManager.SetFlat(TDBID.Create(prefix + "transitionTo"), strings);
    TweakDBManager.SetFlat(TDBID.Create(prefix + "transitionCondition"), conditions);
    for field in fields {
      flags = TweakDBInterface.GetBoolArray(TDBID.Create(prefix + field));
      if ArraySize(flags) > 0 {
        ArrayInsert(flags, 0, false);
        TweakDBManager.SetFlat(TDBID.Create(prefix + field), flags);
      };
    };
  }
}
