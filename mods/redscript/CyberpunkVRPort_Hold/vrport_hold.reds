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

// THE SAME ATTACH, IN THE SCENE PLANE, and it exists because the plane decides whether the engine's
// own outline can ever reach the prop. Measured 2026-09-02, on one asset with everything else held
// constant: spawned free in the world it outlines on `RPl_Scene` and does not on `RPl_Weapon`, and
// once attached through the call above -- which passes `RPl_Weapon` -- it stops outlining even though
// its components still report `RPl_Scene`. The plane the game draws with is this argument, not the
// template's, exactly as the note above says.
//
// The trade-off is the one already recorded for carriers: in first person the gun and the arms are
// drawn in `RPl_Weapon` and are not in the world's depth, so a prop in the scene plane is composited
// against them and the gun can show through it. That is why this is a SECOND method rather than a
// change to the first -- the speedloader, which is only ever held in front of the gun, keeps the
// weapon plane; a belt element, which hangs at the hip, can afford the scene plane and gains the
// outline by it.
@addMethod(PlayerPuppet) public func VRPortHoldItemScene(item: String, slot: String) -> Bool {
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
  return ts.AddItemToSlot(this, TDBID.Create(slot), id, true, null, ERenderingPlane.RPl_Scene, false, false);
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

// THE SAME ATTACH WITH EVERY ARGUMENT FILLED, for items the short form does not draw.
//
// A grenade is the case that forced this. Attached with the call above it lands in the slot for real
// -- the transaction system reports it there, with 23 components, 5 skinned meshes and a vision
// component -- and nothing is drawn. A pistol attached the same way, with the same empty
// `appearanceName`, IS drawn. So the difference is not the appearance list and not the slot, and the
// grenade's tags say what it probably is: `PreemptivePooled`, an object the game builds its visual for
// on its own path rather than on a plain attach.
//
// The native takes more than the short form passes:
//
//     AddItemToSlot(obj, slotID, itemID, highPriority, itemObject, plane, keepWorldTransform,
//                   ignoreRestrictions, garmentAppearanceName, appearanceItem, dontCacheEquippedItem)
//
// `ignoreRestrictions` and `appearanceItem` are the two that could plausibly matter here, so this
// variant fills them and leaves everything else as the working form has it. If the grenade draws with
// this and not with the short one, the answer is in whichever of the two it needed.
@addMethod(PlayerPuppet) public func VRPortHoldItemFull(item: String, slot: String) -> Bool {
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
  return ts.AddItemToSlot(this, TDBID.Create(slot), id, true, null, ERenderingPlane.RPl_Scene,
                          false, true, n"", id, false);
}

// ---------------------------------------------------------------------------------------------
// THE THROW.
//
// WHY THE GAME'S OWN BUTTON IS NOT ENOUGH: it launches down the CAMERA. The port's projectile hook
// (OrientationProvider slot 30) can steer a launch, but it is gated on the player having a weapon out
// with a published muzzle -- with a grenade in an otherwise empty hand that gate is shut, which is
// exactly what "кидает по траектории игры" is.
//
// So the throw is not steered, it is ISSUED. `gameprojectileSetUpAndLaunchEvent` carries everything
// this needs in one event, and the game itself uses it for precisely this: `GrenadeLvl4HackEffector`
// takes a grenade out of a slot and launches it with providers it builds by hand.
//
//     logicalPositionProvider     where it starts   -> the grenade's own entity, i.e. the hand
//     logicalOrientationProvider  which way         -> a STATIC provider from our direction vector
//     ownerVelocityProvider       the player's own  -> so a throw made while running carries
//     trajectoryParams            how fast          -> parabolic, our speed, the grenade's gravity
//
// Nothing here is a TweakDB write, so no other grenade in the world is affected by one throw.
//
// The DIRECTION IS WORLD SPACE. The caller measures the hand in model space, where the port does all
// its geometry, and rotates it out by the player's orientation before calling -- doing it here would
// mean this function had to know which space it was handed.
@addMethod(PlayerPuppet) public func VRPortThrowGrenade(slot: String,
                                                        dx: Float, dy: Float, dz: Float,
                                                        speed: Float) -> Bool {
  let ts = this.VRPortHoldTS();
  let slotID = TDBID.Create(slot);
  let item = ts.GetItemInSlot(this, slotID);
  if !IsDefined(item) {
    return false;
  };
  let dir = new Vector4(dx, dy, dz, 0.0);
  if Vector4.Length(dir) < 0.001 {
    return false;
  };
  let q = Quaternion.BuildFromDirectionVector(dir);

  // GRAVITY IS THE GRENADE'S OWN, read off the item rather than assumed: the delivery methods differ
  // (a sticky grenade does not fall like a frag one), and it is a single call on the item that is
  // being thrown. -9.81 is only the fallback for an item that is not a grenade at all.
  let gravity: Float = -9.81;
  let grenade = item as BaseGrenade;
  if IsDefined(grenade) {
    let g = grenade.GetAccelerationZ();
    if AbsF(g) > 0.001 {
      gravity = g;
    };
  };

  // OUT OF THE SLOT FIRST, exactly as the effector does it: while the item is attached it is the
  // hand's, and a projectile cannot be both held and in flight.
  ts.RemoveItemFromSlot(this, slotID, false);

  let ev = new gameprojectileSetUpAndLaunchEvent();
  ev.launchParams.logicalPositionProvider = IPositionProvider.CreateEntityPositionProvider(item);
  ev.launchParams.logicalOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(q);
  ev.launchParams.visualPositionProvider = IPositionProvider.CreateEntityPositionProvider(item);
  ev.launchParams.visualOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(q);
  ev.launchParams.ownerVelocityProvider =
      MoveComponentVelocityProvider.CreateMoveComponentVelocityProvider(this);
  ev.trajectoryParams =
      ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), speed);
  ev.owner = this;
  item.QueueEvent(ev);
  return true;
}

// Is anything in that slot yet? The attach lands on the NEXT frame -- the note at the top of this file
// is about exactly that -- so the throw has to wait for the real grenade to appear before it can
// launch it, and this is what the waiting looks from Lua.
@addMethod(PlayerPuppet) public func VRPortSlotFilled(slot: String) -> Bool {
  return IsDefined(this.VRPortHoldTS().GetItemInSlot(this, TDBID.Create(slot)));
}

// THE SAME THROW, but leaving from the hand that actually threw it.
//
// `VRPortThrowGrenade` launches from the ITEM's own position, and the item lives in
// AttachmentSlots.WeaponLeft -- the game's own slot for a grenade, and the only one its record allows
// without help. Thrown with the right hand that means the grenade appears out of the other hand,
// which is most of "сам бросок странно выглядит".
//
// The fix is not to move the item. The game's own thrown knife does not launch from wherever the item
// sits either -- `ProjectileLaunchHelper.SpawnProjectileFromRightHand` builds
// `IPositionProvider.CreateSlotPositionProvider(owner, n"RightHand")` -- so the launch is simply told
// which hand slot on the PLAYER it comes out of, and the item can stay where it is.
@addMethod(PlayerPuppet) public func VRPortThrowGrenadeFrom(slot: String, handSlot: String,
                                                            dx: Float, dy: Float, dz: Float,
                                                            speed: Float) -> Bool {
  let ts = this.VRPortHoldTS();
  let slotID = TDBID.Create(slot);
  let item = ts.GetItemInSlot(this, slotID);
  if !IsDefined(item) {
    return false;
  };
  let dir = new Vector4(dx, dy, dz, 0.0);
  if Vector4.Length(dir) < 0.001 {
    return false;
  };
  let q = Quaternion.BuildFromDirectionVector(dir);

  let gravity: Float = -9.81;
  let grenade = item as BaseGrenade;
  if IsDefined(grenade) {
    let g = grenade.GetAccelerationZ();
    if AbsF(g) > 0.001 {
      gravity = g;
    };
  };

  ts.RemoveItemFromSlot(this, slotID, false);

  // SET IT UP AS A PROJECTILE FIRST, and this is not belt-and-braces.
  //
  // `BaseGrenade.OnProjectileInitialize` is where a grenade becomes one: it reads its own tweak record,
  // calls Reset() -- which is what sets `m_isAlive` and starts `InitializeRotation`, the tumble a
  // thrown grenade has -- sets the energy-loss factor, the explosion radius, the collision evaluator
  // and the sinking depth. Every one of those is what a throw looks and behaves like.
  //
  // That callback handles `gameprojectileSetUpEvent`. What we were sending is
  // `gameprojectileSetUpAndLaunchEvent`, which despite the name extends `gameprojectileLaunchEvent` and
  // is a different type -- so the script side of the setup may never have run at all. That fits what
  // was seen: a grenade that flies without tumbling, looks wrong, and behaves like nothing in
  // particular. Sending the real setup event costs one queued event and a second Reset() at worst.
  let su = new gameprojectileSetUpEvent();
  su.owner = this;
  su.weapon = this;
  su.trajectoryParams =
      ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), speed);
  item.QueueEvent(su);

  let from = IPositionProvider.CreateSlotPositionProvider(this, StringToName(handSlot));
  let ev = new gameprojectileSetUpAndLaunchEvent();
  ev.launchParams.logicalPositionProvider = from;
  ev.launchParams.logicalOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(q);
  ev.launchParams.visualPositionProvider = from;
  ev.launchParams.visualOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(q);
  ev.launchParams.ownerVelocityProvider =
      MoveComponentVelocityProvider.CreateMoveComponentVelocityProvider(this);
  ev.trajectoryParams =
      ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), speed);
  ev.owner = this;
  item.QueueEvent(ev);
  return true;
}

// BLOW UP THE REAL GRENADE AT A GIVEN POINT IN THE WORLD.
//
// WHY THIS EXISTS. What the game throws is not the item: measured live, the ItemObject is DESTROYED
// the instant the launch event is taken, and what flies is an entity of the game's own that nothing
// on this side can reach. Ours could therefore never be seen -- a grenade with skinned meshes and no
// live skeleton draws as a ghost, which is exactly what was on the picture.
//
// So the port throws its OWN mesh, with its own physics, exactly as it already throws a magazine --
// and when the fuse runs out, this puts the real thing at that point and detonates it. The damage,
// the radius, the effect, the sound and the quest fact are all the game's own, because it IS the
// game's own grenade going off.
//
//   CreateStaticPositionProvider   an arbitrary world point, which is what the flight ends at
//   speed 0.01                     it must not travel: a launch of zero velocity is refused outright
//                                  (SetParabolicLaunchTrajectory returns false for vel <= 0)
//   ForceActivation                `BaseGrenade.OnForceActivation` sets m_forceExplosion, which is
//                                  the game's own "go off now" and nothing else
@addMethod(PlayerPuppet) public func VRPortDetonateGrenadeAt(slot: String,
                                                             x: Float, y: Float, z: Float) -> Bool {
  let ts = this.VRPortHoldTS();
  let slotID = TDBID.Create(slot);
  let item = ts.GetItemInSlot(this, slotID);
  if !IsDefined(item) {
    return false;
  };
  let gravity: Float = -9.81;
  let grenade = item as BaseGrenade;
  if IsDefined(grenade) {
    let g = grenade.GetAccelerationZ();
    if AbsF(g) > 0.001 {
      gravity = g;
    };
  };

  let wp: WorldPosition;
  WorldPosition.SetXYZ(wp, x, y, z);
  let at = IPositionProvider.CreateStaticPositionProvider(wp);
  let down: Quaternion;
  Quaternion.SetIdentity(down);

  ts.RemoveItemFromSlot(this, slotID, false);

  let su = new gameprojectileSetUpEvent();
  su.owner = this;
  su.weapon = this;
  su.trajectoryParams =
      ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), 0.01);
  item.QueueEvent(su);

  let ev = new gameprojectileSetUpAndLaunchEvent();
  ev.launchParams.logicalPositionProvider = at;
  ev.launchParams.visualPositionProvider = at;
  ev.launchParams.logicalOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(down);
  ev.launchParams.visualOrientationProvider = IOrientationProvider.CreateStaticOrientationProvider(down);
  ev.trajectoryParams =
      ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), 0.01);
  ev.owner = this;
  item.QueueEvent(ev);
  item.QueueEvent(new gameprojectileForceActivationEvent());
  return true;
}

// SPEND A GRENADE THE WAY THE GAME SPENDS ONE.
//
// NOT `RemoveItem`. That deletes the item -- the whole stack entry goes, the equipped type switches to
// whatever is left, and nothing ever comes back. Measured on a live save, and it cost a real grenade
// to find out: 1 -> 0 and the frag was gone from the inventory for good.
//
// Since 2.0 a grenade is a CHARGE, not an item taken off a pile: `gamedataStatPoolType.GrenadesCharges`
// is a 0..100 pool, one throw costs `GetGrenadeThrowCostClean()` (35 on this save, so three throws),
// and the pool refills on its own over time. That is exactly what a player means by "она консьюмится,
// а потом по времени игра её сама восполняет" -- and the item stays in the inventory throughout,
// because the item was never what was being spent.
//
// This is the same call `ConsumablesChargesHelper` makes to REFRESH charges, with the sign turned
// round; the cast it needs is a redscript one, which is why this is not in Lua.
@addMethod(PlayerPuppet) public func VRPortSpendGrenadeCharge() -> Bool {
  let cost = this.GetGrenadeThrowCostClean();
  if cost <= 0 {
    return false;
  };
  GameInstance.GetStatPoolsSystem(this.GetGame())
    .RequestChangingStatPoolValue(Cast<StatsObjectID>(this.GetEntityID()),
                                  gamedataStatPoolType.GrenadesCharges,
                                  -Cast<Float>(cost), this, false, false);
  return true;
}

// How many throws are left, for the belt: a grenade with no charge is not on the belt, and the
// inventory count is the wrong question -- the item stays there while the charges run out.
@addMethod(PlayerPuppet) public func VRPortGrenadeCharges() -> Int32 {
  return this.GetGrenadeCharges();
}
