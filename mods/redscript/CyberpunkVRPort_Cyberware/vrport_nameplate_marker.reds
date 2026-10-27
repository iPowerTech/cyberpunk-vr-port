public native func VRWorldMarkerProjection(widget: ref<inkWidget>, projection: ref<inkScreenProjection>) -> Void;

@wrapMethod(NpcNameplateGameController)
protected cb func OnScreenProjectionUpdate(projections: ref<gameuiScreenProjectionsData>) -> Bool {
  let result = wrappedMethod(projections);
  let selected: ref<inkScreenProjection>;
  // Native X placement uses projection 0 for puppets and 2 for devices.
  // Track that exact projection (including its slot and world offsets).
  if IsDefined(this.m_nameplateProjection.GetEntity()) {
    selected = this.m_nameplateProjection;
    if IsDefined(this.m_bufferedGameObject) && this.m_bufferedGameObject.IsDevice() {
      selected = this.m_nameplateProjectionDevice;
    };
  };
  VRWorldMarkerProjection(inkWidgetRef.Get(this.projection), selected);
  return result;
}

// THE NAMEPLATE KEEPS ITS HANDS OFF THE MAPPIN ICONS. The one NPC nameplate follows the NPC next to the crosshair,
// in the headset wherever the head points, and slots that NPC's mappin roots (stealth, quest, quick hack icons) into
// its mappinSlot under its own anchor (UpdateSlotAttachment), while OnNameplate switches their screen projection and
// distance scale off. Every glance at an NPC in nameplate range moved its icon about 115 design units (~5 deg) down
// into the slot and back up on looking away: measured on 2.5, 9 swaps in 5 s of head turns next to one NPC. The
// icons stay at their own world point; the nameplate keeps its name and health bar.
@replaceMethod(BaseMappinBaseController)
public func GetWidgetForNameplateSlot() -> wref<inkWidget> {
  return null;
}

@replaceMethod(StealthMappinController)
public func GetWidgetForNameplateSlot() -> wref<inkWidget> {
  return null;
}

@wrapMethod(StealthMappinController)
protected cb func OnNameplate(isNameplateVisible: Bool, nameplateController: wref<NpcNameplateGameController>) -> Bool {
  let result = wrappedMethod(isNameplateVisible, nameplateController);
  if isNameplateVisible && (this.m_isAggressive || this.m_isHostile || this.m_isTurret) {
    this.OverrideScaleByDistance(true);
    this.SetProjectToScreenSpace(true);
  };
  return result;
}

@wrapMethod(QuestMappinController)
protected cb func OnNameplate(isNameplateVisible: Bool, nameplateController: wref<NpcNameplateGameController>) -> Bool {
  let result = wrappedMethod(isNameplateVisible, nameplateController);
  if isNameplateVisible {
    this.OverrideScaleByDistance(true);
    this.SetProjectToScreenSpace(true);
  };
  return result;
}

@wrapMethod(QuickHackMappinController)
protected cb func OnNameplate(isNameplateVisible: Bool, nameplateController: wref<NpcNameplateGameController>) -> Bool {
  let result = wrappedMethod(isNameplateVisible, nameplateController);
  if isNameplateVisible {
    this.SetProjectToScreenSpace(true);
  };
  return result;
}
