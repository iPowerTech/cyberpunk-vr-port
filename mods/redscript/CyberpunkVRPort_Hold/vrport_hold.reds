// CyberpunkVRPort — HOLD AN ITEM IN A HAND, from script.
//
// WHY THIS FILE EXISTS, measured rather than assumed (2026-08-16):
//
//     RemoveItemFromSlot   from CET   works -- a cigarette in WeaponRight was taken out by it
//     AddItemToSlot (4 args) from CET returns TRUE and attaches nothing
//     AddItemToSlot (8 args) from CET returns TRUE and attaches nothing
//     AddItemToSlot        from reds  works -- the prop is in the hand on the next frame
//
// So the handle, the TweakDBID and the Bool all cross the CET boundary intact -- Remove proves it,
// it takes the same three -- and it is `AddItemToSlot` in particular that does not take, whatever
// argument count it is given. It is the only one of the two carrying a `gameItemID` and a
// `whandle:gameItemObject`. The cause is inside CET's own call frame for that function and chasing it
// buys nothing: this wrapper is twenty lines and is how the rest of this port already drives the
// game -- holster, melee and smoking are all redscript for the same reason.
//
// WHAT IT IS FOR. Most things the reload puts in a hand are ONE mesh, and those ride a carrier
// component on the player's own template (vrp_hold_left / vrp_hold_right): exact by construction,
// because the engine draws them in the same pass as the hand, and needing no item at all. A
// speedloader is not one mesh -- the Overture's is seven, six rounds and a handle, and it lives in
// its own asset rather than on the weapon, so there is no single mesh to carry and no bone of the
// weapon's to drive. What the game does with a many-part thing in a hand is carry it as an ITEM:
// `grenadeLvl4HackEffector` puts a grenade in `AttachmentSlots.WeaponLeft` exactly this way.
//
// Names cross as STRINGS on purpose: a TweakDBID built on the far side is one more thing that can be
// silently wrong, and `TDBID.Create` is the same resolution the game uses.
//
// Driven from Lua like every other helper here:
//   Game.GetPlayer():VRPortHoldItem("Items.VRPortLoadOverture", "AttachmentSlots.WeaponLeft")
//   Game.GetPlayer():VRPortDropItem("AttachmentSlots.WeaponLeft")
//   Game.GetPlayer():VRPortHasItem("AttachmentSlots.WeaponLeft")

@addMethod(PlayerPuppet) private func VRPortHoldTS() -> ref<TransactionSystem> {
  return GameInstance.GetTransactionSystem(this.GetGame());
}

// Give the item if it is not carried yet, then put it in the slot. Returns what the game returned:
// the caller is expected to check rather than assume, which is the whole lesson above.
@addMethod(PlayerPuppet) public func VRPortHoldItem(item: String, slot: String) -> Bool {
  let ts = this.VRPortHoldTS();
  let id = ItemID.FromTDBID(TDBID.Create(item));
  if !ItemID.IsValid(id) {
    return false;
  };
  if !ts.HasItem(this, id) {
    if !ts.GiveItem(this, id, 1) {
      return false;
    };
  };
  // highPriority: the same argument the smoking module uses, and what makes a prop win the slot
  // instead of queueing behind whatever the equipment system has in mind for it.
  //
  // AND THE RENDERING PLANE IS DECIDED HERE, not afterwards. In first person the gun and the arms are
  // drawn in `RPl_Weapon`, which is what keeps them out of the world's depth and lighting; a prop left
  // in the scene plane is composited against them and you can see the gun through it. Setting
  // `renderingPlane` on the item's components after the attach does NOT work -- measured, the prop
  // stayed transparent -- because the game applies the plane when it takes the item, from this very
  // argument. The two `false`s between are keepWorldTransform and ignoreRestrictions, at their
  // defaults; they are only spelled out because the plane is the sixth.
  return ts.AddItemToSlot(this, TDBID.Create(slot), id, true, null, ERenderingPlane.RPl_Weapon, false, false);
}

// Take back whatever is in that slot. `false` leaves the item in the inventory rather than
// destroying it, so the next grab costs no GiveItem.
@addMethod(PlayerPuppet) public func VRPortDropItem(slot: String) -> Bool {
  this.VRPortHoldTS().RemoveItemFromSlot(this, TDBID.Create(slot), false);
  return true;
}

// Is that slot holding anything -- so a caller can tell ATTACHED from "the call was accepted", which
// are not the same thing and were exactly what the CET route confused.
@addMethod(PlayerPuppet) public func VRPortHasItem(slot: String) -> Bool {
  return IsDefined(this.VRPortHoldTS().GetItemInSlot(this, TDBID.Create(slot)));
}
