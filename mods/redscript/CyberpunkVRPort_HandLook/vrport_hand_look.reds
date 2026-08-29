// CyberpunkVRPort -- does the interaction follow the LOOK-AT, or does it have a path of its own?
//
// WHY THIS FILE EXISTS. The goal is to let the HAND choose what gets activated, the way a hand does
// in a native VR title, instead of the head choosing and the hand only deciding when. Before any of
// that can be built, one thing has to be known, and it cannot be reasoned out from the outside:
//
//     the game picks an interactable with its own query -- is the result of that query the PLAYER'S
//     LOOK-AT OBJECT, or does the interaction system run a search of its own?
//
// The RTTI says where to ask. gametargetingTargetingSystem::GetLookAtObject is NATIVE, and its
// implementation is not reachable from the function object -- this port's own natives prove it: the
// address of an implementation that is known to live in the plugin appears nowhere in the object, so
// the game dispatches through a table indexed by regIndex. PlayerPuppet::UpdateLookAtObject, on the
// other hand, is SCRIPTED -- its vtable and its bytecode runner differ from the native pair -- so it
// can be replaced from here, with no engine hook and nothing to re-find after a patch.
//
// THE EXPERIMENT, and it is deliberately the smallest one that can answer: with MODE = 1 the look-at
// is forced to nothing. Then look at a door, a terminal, a shard:
//
//   * the interaction prompt DISAPPEARS -> interactions are driven by the look-at.
//   * the interaction prompt STAYS -> the interaction system has its own search.
//
// BOTH READINGS WERE WRONG, because the wrapped function is not the look-at -- see the correction
// below. The prompt was always going to stay, whatever the interaction system does.
//
// Either answer is worth the ten lines. The failure mode of guessing here is a week of building on
// the wrong half.
//
// MODE 0 restores the vanilla behaviour and leaves nothing of this file in the way.

module CyberpunkVRPort.HandLook

// THE EXPERIMENT HAS BEEN RUN, AND ITS CONCLUSION WAS WITHDRAWN. It is left here in full, with the
// correction, because a retracted result that is quietly deleted gets rediscovered.
//
// WHAT IT CLAIMED. With the look-at forced to nothing the prompt still appeared, so -- it said -- the
// interaction system does not read the player's look-at object and runs a search of its own.
//
// WHY THAT DOES NOT FOLLOW. The decompiled game source settles it (player.script:779):
// PlayerPuppet.UpdateLookAtObject writes three script fields and nothing else --
// m_isAimingAtFriendly, m_isAimingAtChild, m_distanceFromTargetSquared. It CONSUMES
// LookAtObjectChangedEvent; it is not the look-at. Passing it an empty handle could never have
// removed anything from the interaction system, so the prompt staying proves nothing at all. The
// probe counter rising proved the wrap ran -- which was the other thing it was there for, and that
// part stands.
//
// WHAT IS TRUE INSTEAD, measured since. The look-at that matters lives in the native TargetingSystem,
// this wrap never touched it, and the interaction DOES follow it: the port now moves that ray to the
// hand at its source (src/Hooks/LookAtRayFromHand.cpp) and the prompt follows the hand.
//
// The file stays because the probe it installs is still the right instrument, and because the way
// this went wrong is worth keeping: the experiment was clean, the measurement was real, and the
// inference from it was not checked against what the wrapped function actually does.
public func VRPortHandLookMode() -> Int32 {
  return 0;
}

// THE SIGNATURE, and the first attempt got it wrong twice over. The RTTI dump reads
// `type=script_ref:handle:gameObject`, but that `script_ref:` is the dumper's own prefix -- it prints
// it in front of everything, including the plain Vector4 this port's own native returns. The type is
// `handle:gameObject`, which is `ref<GameObject>`. And the method is not `final`: redscript matches
// qualifiers as part of the signature, so a wrong one fails exactly like a wrong type.
@wrapMethod(PlayerPuppet)
protected func UpdateLookAtObject(target: ref<GameObject>) -> Void {
  // DID THIS EVEN RUN? A prompt that stays put has two explanations -- the interaction does not use
  // the look-at, or this wrap never fired -- and they call for opposite next moves. So the call is
  // COUNTED, into a shared slot the panel already shows, and the question stops being a matter of
  // impression. The slot is the port's own diagnostic channel; nothing else writes it.
  SetVRProbe(0, 1.0);
  if VRPortHandLookMode() == 1 {
    // Not "skip the call": the vanilla method does more than store the target -- it raises the
    // change event other systems listen to. Passing an empty handle keeps all of that intact and
    // changes only the answer, which is what makes the observation clean.
    let none: ref<GameObject>;
    wrappedMethod(none);
    return;
  }
  wrappedMethod(target);
}
