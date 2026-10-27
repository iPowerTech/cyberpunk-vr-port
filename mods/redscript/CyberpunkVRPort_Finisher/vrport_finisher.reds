// CyberpunkVRPort -- finishers, reported to the plugin.
//
// A finisher with its paired animation (blade/blunt finishers on a low-health enemy) runs the player
// in a workspot tagged FinisherWorkspot; PlayerPuppet raises m_isInFinisher when it starts and drops it
// when it finishes. VRIK steps aside for that animation exactly as for a grapple
// (src/Hooks/AnimPose.cpp, CyberpunkVR_VrikFinisherSuspend). A fast finisher is an ordinary melee hit
// and starts no workspot.
//
// This file intentionally stays in the global namespace: the plugin registers plain global natives.

native func SetVRFinisherActive(active: Bool) -> Int32;

@wrapMethod(PlayerPuppet)
protected cb func OnWorkspotStartedEvent(evt: ref<WorkspotStartedEvent>) -> Bool {
  let result: Bool = wrappedMethod(evt);
  SetVRFinisherActive(this.GetIsInWorkspotFinisher());
  return result;
}

@wrapMethod(PlayerPuppet)
protected cb func OnWorkspotFinishedEvent(evt: ref<WorkspotFinishedEvent>) -> Bool {
  let result: Bool = wrappedMethod(evt);
  SetVRFinisherActive(this.GetIsInWorkspotFinisher());
  return result;
}
