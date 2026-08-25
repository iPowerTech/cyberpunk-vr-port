// CyberpunkVRPort -- FIREARMS STAY IN YOUR HANDS. Folded into this mod on 2026-08-20; everything below
// the second banner is Rivarez's original file, byte for byte, authorship banner included.
//
// WHY THE PORT NEEDS IT. The game lowers a drawn firearm after a while of no weapon input, and then
// unequips it entirely. The port's aiming and firing go through the controllers, so from the state
// machine's point of view the player is not using the weapon at all -- and the gun lowers itself in
// your hands, or vanishes. This file pins the raised/lowered state instead, raises on a fire or aim
// tap, and lowers on the holster button.
//
// HOW IT SITS WITH CyberpunkVRPort_WeaponUp. Ours was written FROM this file -- its own header says so
// -- because Rivarez's covers firearms only, behind an IsFirearms gate, and states that it does not
// work for melee. WeaponUp adds the melee half on the same two hook points. Checked rather than
// assumed, across all 26 wraps in this port:
//
//   * both wrap DefaultTransition.ShouldEnterSafe. Redscript CHAINS wraps rather than rejecting them,
//     and the two gates are mutually exclusive (a firearm in the right hand vs a melee weapon), so
//     only one of them ever answers for a given weapon.
//   * the @replaceMethod here is PublicSafeEvents.RequestWeaponUnequipNotifyUpperBody; ours is
//     MeleePublicSafeEvents.OnTick. Different classes, so no duplicate replace.
//   * ReadyEvents.OnTick and EquipmentBaseTransition.HandleWeaponEquip are wrapped by this file and by
//     vrport_no_anims.reds. Both are wraps; they chain.
//
// WHERE IT CAME FROM. A loose file in r6\scripts (v1.1, dated 2023-12-16) that the port silently
// depended on. The loose copy was DELETED in the same change -- with both present there would be two
// @replaceMethod on PublicSafeEvents.RequestWeaponUnequipNotifyUpperBody, which does not compile.
//
// THIRD-PARTY CODE. Written by Rivarez, not by this project. The banner and the contact address below
// are left exactly as they were; if the port is redistributed, that is the author to ask.
//
// ============================== ORIGINAL FILE FOLLOWS, UNMODIFIED ==============================

/* ============== Control lower and holster weapons for Cyberpunk 2077 ==============
                                      v.1.1
   ==================== Created by Rivarez (rivarez@list.ru) ========================

    M     M      A      DDDD   EEEEE     I  N    N     U    U   SSSS    SSSS   RRRR
    M M M M     A A     D   D  E         I  N N  N     U    U  S       S       R   R
    M  M  M    A   A    D   D  EEEE      I  N  N N     U    U   SSSS    SSSS   RRRR
    M     M   A A A A   D   D  E         I  N   NN     U    U       S       S  R  R
    M     M  A       A  DDDD   EEEEE     I  N    N      UUUU    SSSS    SSSS   R   R
*/


// возвращает true, если оружие опущено (или должно быть опущено)
public func IsWeaponLowered(stateContext: ref<StateContext>) -> Bool
{
  let safeTimeStamp: Float = stateContext.GetFloatParameter(n"TurnOffPublicSafeTimeStamp", true);
  return safeTimeStamp == 0.0;
}

// возвращает true, если оружие поднято (или должно быть поднято)
public func IsWeaponRaised(stateContext: ref<StateContext>) -> Bool
{
  let safeTimeStamp: Float = stateContext.GetFloatParameter(n"TurnOffPublicSafeTimeStamp", true);
  return safeTimeStamp == 9999999.0;
}

/* Опускание(не убирание) оружия происходит по истечению интервала времени с момента TurnOffPublicSafeTimeStamp. Поэтому устанавливаем такое
   значение для этой переменной, чтобы интервал времени с момента TurnOffPublicSafeTimeStamp никогда бы не закончился.
   DontSetIfLowered - не устанавливать, если мы вручную(нажатием на кнопку) опустили оружие */
public func SetWeaponRaised(stateContext: ref<StateContext>, DontSetIfLowered: Bool) -> Void
{
  if DontSetIfLowered && IsWeaponLowered(stateContext)
  {
    return;
  };
  stateContext.SetPermanentFloatParameter(n"TurnOffPublicSafeTimeStamp", 9999999.0, true);
}

/* Здесь наоборот - устанавливаем ноль для TurnOffPublicSafeTimeStamp чтобы интервал времени с момента TurnOffPublicSafeTimeStamp
   всегда превышал бы значение, после которого оружие опускается. Тем самым мы и опускаем оружие. */
public func SetWeaponLowered(stateContext: ref<StateContext>) -> Void
{
  stateContext.SetPermanentFloatParameter(n"TurnOffPublicSafeTimeStamp", 0.0, true);
}


// ============================================================================================================================================
// Подменяем все методы, где устанавливается TurnOffPublicSafeTimeStamp и устанавливаем там свои значения для этой переменной в соответствии с описанным выше
@wrapMethod(EquipmentBaseTransition)
  protected final const func HandleWeaponEquip(scriptInterface: ref<StateGameScriptInterface>, stateContext: ref<StateContext>, stateMachineInstanceData: StateMachineInstanceData, item: ItemID) -> Void
  {
    wrappedMethod(scriptInterface, stateContext, stateMachineInstanceData, item);
    SetWeaponRaised(stateContext, false);
  }

@wrapMethod(ReadyEvents)
  protected final func OnTick(timeDelta: Float, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void
  {
    wrappedMethod(timeDelta, stateContext, scriptInterface);
    SetWeaponRaised(stateContext, true);
  }

@wrapMethod(PublicSafeToReadyEvents)
  protected final func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void
  {
    wrappedMethod(stateContext, scriptInterface);
    SetWeaponRaised(stateContext, true);
  }

@wrapMethod(QuickMeleeEvents)
  protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void
  {
    wrappedMethod(stateContext, scriptInterface);
    SetWeaponRaised(stateContext, false);
  }

@wrapMethod(ShootEvents)
  protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void
  {
    wrappedMethod(stateContext, scriptInterface);
    SetWeaponRaised(stateContext, false);
  }
// ============================================================================================================================================

// чтобы оружие автоматически не убиралось через некоторое время
@replaceMethod(PublicSafeEvents)
  private final func RequestWeaponUnequipNotifyUpperBody(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  }

// возвращает true, если это огнестрел
public func IsFirearms(scriptInterface: ref<StateGameScriptInterface>) -> Bool
{
  let weapon: ref<WeaponObject> = scriptInterface.GetTransactionSystem().GetItemInSlot(scriptInterface.executionOwner, t"AttachmentSlots.WeaponRight") as WeaponObject;
  return IsDefined(weapon) && weapon.IsRanged();
}

// делаем поднятие и опускание оружия по нажатию на кнопку
@wrapMethod(UpperBodyEventsTransition)
  protected final func UpdateSwitchItem(timeDelta: Float, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Bool
  {
    if IsFirearms(scriptInterface)
    {
      // поднятие нажатием на кнопку стрельбы или прицеливания
      let FireBtnTapped: Bool = scriptInterface.IsActionJustTapped(n"RangedAttack");
      let AimBtnTapped: Bool = scriptInterface.GetActionValue(n"CameraAim") > 0.00;
      if FireBtnTapped || AimBtnTapped
      {
        SetWeaponRaised(stateContext, false);
        return false;
      };

      // опускание поднятого оружия нажатием на кнопку убрать/вытащить оружие
      let holsterBtnTapped: Bool = scriptInterface.IsActionJustTapped(n"HolsterWeapon");
      if holsterBtnTapped && IsWeaponRaised(stateContext)
      {
        SetWeaponLowered(stateContext);
        return false;
      };
    };

    return wrappedMethod(timeDelta, stateContext, scriptInterface);
  }

/* Метод разрешает или запрещает оружию выходить из опущенного состояния. С помощью этого метода предотвращаем автоматическое поднятие оружия
   при входе в определенные зоны. Также с помощью этого метода поднимаем оружие в опасных зонах */
@wrapMethod(PublicSafeDecisions)
  private final const func ShouldLeaveSafe(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool
  {
    if IsFirearms(scriptInterface)
    {
      if IsWeaponRaised(stateContext)  // если оружие должно быть поднято
      {
        return true;                   // то разрешаем выйти из опущенного состояния
      };
      if IsWeaponLowered(stateContext)  // если оружие должно быть опущено
      {
        return false;                   // то запрещаем выход из опущенного состояния
      };
    };

    return wrappedMethod(stateContext, scriptInterface);
  }

/* Этот метод, в отличии от метода выше, разрешает или запрещает оружию входить в опущенное состояние. С помощью этого метода предотвращаем автоматическое опускание
   оружия при входе в эти зоны. Также с помощью этого метода опускаем оружие в опасных зонах */
@wrapMethod(DefaultTransition)
  protected final const func ShouldEnterSafe(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>, const opt maxDistanceSquared: Float) -> Bool
  {
    if IsFirearms(scriptInterface)
    {
      if IsWeaponRaised(stateContext)  // если оружие должно быть поднято
      {
        return false;                  // то запрещаем его опускать
      };
      if IsWeaponLowered(stateContext)  // если оружие должно быть опущено
      {
        return true;                    // то разрешаем вход в опущенное состояние
      };
    };

    return wrappedMethod(stateContext, scriptInterface, maxDistanceSquared);
  }