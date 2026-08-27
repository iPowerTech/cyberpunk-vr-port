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
//   * the interaction prompt DISAPPEARS -> interactions are driven by the look-at, and the hand can
//     take it over from this very function: feed it the entity the hand ray hits and everything
//     downstream -- prompt, choices, hold interactions -- follows by itself.
//   * the interaction prompt STAYS -> the interaction system has its own search, this path is a dead
//     end for it, and the hunt goes back to the native side with the regIndex table as its start.
//
// Either answer is worth the ten lines. The failure mode of guessing here is a week of building on
// the wrong half.
//
// MODE 0 restores the vanilla behaviour and leaves nothing of this file in the way.

module CyberpunkVRPort.HandLook

// THE EXPERIMENT HAS BEEN RUN, AND THE ANSWER IS NO. With the look-at forced to nothing, the probe
// counter climbed exactly as the camera swept across an object -- so this wrap fires on every look-at
// change and the suppression was real -- and the interaction prompt appeared regardless. The
// interaction system does not read the player's look-at object; it runs a search of its own.
//
// Left at 0 and kept: the file is the record of that measurement, and the probe it installs is how the
// next experiment on this path will avoid the same ambiguity. 1 reruns it.
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
