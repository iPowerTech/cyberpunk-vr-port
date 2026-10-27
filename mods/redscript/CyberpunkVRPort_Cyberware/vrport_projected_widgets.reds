// THE WIDGETS THE GAME PROJECTS ONTO THE SCREEN HANG IN THE WORLD. Damage numbers and chatter lines are placed at
// their screen projection (inkScreenProjection.currentPosition) by their own controllers. The UI is laid out in one
// eye's window under the engine's stereo (src/Stereo/EngineStereo.cpp THE UI IS LAID OUT FOR ONE EYE), and the
// UI camera those positions come from is the head's, the union of the two eye windows: flat, they sat off their
// targets and at infinity in both eyes. Each one names its projection, as the NPC nameplate does
// (vrport_nameplate_marker.reds), and is lifted to the projection's world point (Hooks/WorldMarkers.cpp Projection).

@wrapMethod(DamageDigitLogicController)
protected cb func OnScreenProjectionUpdate(projection: ref<inkScreenProjection>) -> Bool {
  let result = wrappedMethod(projection);
  VRWorldMarkerProjection(this.m_rootWidget, projection);
  return result;
}

@wrapMethod(AccumulatedDamageDigitLogicController)
protected cb func OnScreenProjectionUpdate(projection: ref<inkScreenProjection>) -> Bool {
  let result = wrappedMethod(projection);
  VRWorldMarkerProjection(this.m_rootWidget, projection);
  return result;
}

@wrapMethod(ChatterLineLogicController)
public final func UpdateProjection(targetedObject: EntityID, owner: wref<ChattersGameController>) -> Void {
  wrappedMethod(targetedObject, owner);
  if IsDefined(this.m_projection) {
    VRWorldMarkerProjection(this.m_rootWidget, this.m_projection);
  };
}
