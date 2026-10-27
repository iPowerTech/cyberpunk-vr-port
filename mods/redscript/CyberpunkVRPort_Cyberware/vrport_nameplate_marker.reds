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
