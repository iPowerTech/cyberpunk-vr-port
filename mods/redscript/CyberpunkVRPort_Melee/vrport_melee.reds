// CyberpunkVRPort — VR motion melee. Treat the weapon's blade as a real collider: a segment from
// the grip to the tip, with a radius "hit zone" around it. Every frame the CET weapon mod passes the
// current blade segment; we find NPCs whose body is within that radius of the segment and fire the
// game's OWN native melee Attack_GameEffect precisely on the touched enemy — native hit REACTIONS,
// material impact VFX (blood), SFX, impulse and native weapon damage. Enhanced
// bonuses are separate multipliers on that native calculation.
//
// Called from CET each frame while swinging a melee weapon:
//   Game.GetPlayer():VRMeleeBladeHit(weapon, bladeStart, bladeEnd, hitRadius, strong)

module CyberpunkVRPort.Melee

// Distance from point p to segment [a,b] (component-wise; no Vector4-operator assumptions).
func VRMeleeDistPointSeg(p: Vector4, a: Vector4, b: Vector4) -> Float {
  let abx = b.X - a.X;  let aby = b.Y - a.Y;  let abz = b.Z - a.Z;
  let apx = p.X - a.X;  let apy = p.Y - a.Y;  let apz = p.Z - a.Z;
  let len2 = abx*abx + aby*aby + abz*abz;
  let t = 0.0;
  if len2 > 0.0001 { t = (apx*abx + apy*aby + apz*abz) / len2; };
  if t < 0.0 { t = 0.0; };
  if t > 1.0 { t = 1.0; };
  let cx = a.X + abx*t;  let cy = a.Y + aby*t;  let cz = a.Z + abz*t;
  let dx = p.X - cx;  let dy = p.Y - cy;  let dz = p.Z - cz;
  return SqrtF(dx*dx + dy*dy + dz*dz);
}

// Min distance from the enemy body axis (sampled at 5 heights from feet to head) to the blade segment.
// More samples = the blade isn't given a "free pass" through the gaps between samples on a tall body,
// which matters when the hit radius is small (so the detection is precise: "blade actually crossed the
// body silhouette", not "blade waved nearby").
func VRMeleeEnemyDist(ent: wref<GameObject>, a: Vector4, b: Vector4) -> Float {
  let base = ent.GetWorldPosition();
  let d0 = VRMeleeDistPointSeg(new Vector4(base.X, base.Y, base.Z + 0.25, 1.0), a, b);  // shin
  let d1 = VRMeleeDistPointSeg(new Vector4(base.X, base.Y, base.Z + 0.7,  1.0), a, b);  // hip
  let d2 = VRMeleeDistPointSeg(new Vector4(base.X, base.Y, base.Z + 1.05, 1.0), a, b);  // chest
  let d3 = VRMeleeDistPointSeg(new Vector4(base.X, base.Y, base.Z + 1.4,  1.0), a, b);  // neck
  let d4 = VRMeleeDistPointSeg(new Vector4(base.X, base.Y, base.Z + 1.65, 1.0), a, b);  // head
  return MinF(MinF(d0, d1), MinF(d2, MinF(d3, d4)));
}

// Use a native melee record, never a ranged/quickhack effect from GetAttacks().
@addMethod(WeaponObject)
public func VRGetMeleeAttack(opt strong: Bool) -> ref<Attack_GameEffect> {
  let a: ref<Attack_GameEffect>;
  if strong { a = this.GetAttack(n"MeleeStrongAttack0") as Attack_GameEffect; };
  if !IsDefined(a) { a = this.GetAttack(n"MeleeComboAttack0") as Attack_GameEffect; };
  if IsDefined(a) && IsDefined(a.GetRecord() as Attack_Melee_Record) { return a; };
  a = this.GetCurrentAttack() as Attack_GameEffect;
  if IsDefined(a) && IsDefined(a.GetRecord() as Attack_Melee_Record) { return a; };
  let attacks = this.GetAttacks();
  for attack in attacks {
    a = attack as Attack_GameEffect;
    if IsDefined(a) && IsDefined(a.GetRecord() as Attack_Melee_Record) { return a; };
  };
  return null;
}

// The weapon's melee reach, straight from the game's attack record (TweakDB Range() — the same value
// the flat game feeds the melee game-effect). No hardcoded blade length.
@addMethod(WeaponObject)
public func VRMeleeReach() -> Float {
  let a = this.VRGetMeleeAttack();
  if !IsDefined(a) { return 1.5; };
  let r = a.GetRecord().Range();
  if r < 0.3 { return 1.5; };
  return r;
}

@addMethod(WeaponObject)
public func VRMeleeDirection() -> Vector4 {
  if WeaponObject.IsOfType(this.GetItemID(), gamedataItemType.Wea_Fists) {
    return this.GetWorldForward();
  };
  // Native handheld melee assets extend along local +Z. For example katana's
  // Impact/reflected slots are at Z=.75/.46/.25 and the bat's impact is Z=.5.
  // Entity +Y is the firearm forward axis and can point below the visible blade.
  return WorldTransform.GetUp(this.GetWorldTransform());
}

@addMethod(PlayerPuppet)
public func VRMeleeBladeHit(weapon: wref<WeaponObject>, bladeStart: Vector4, bladeDir: Vector4, hitRadius: Float, strong: Bool, opt passive: Bool) -> Int32 {
  if !IsDefined(weapon) { return 0; };
  // Holster guard: while a melee holster was just requested, reaching the hand to the holster zone is
  // motion that would trip the swing threshold and fire an attack — which leaves melee in an attack/busy
  // state and makes the UnequipWeapon get dropped. Suppress swings for the holster window so the unequip
  // lands. (This is why script-unequip "didn't work" for melee but the keyboard B does: B isn't a swing.)
  if this.VRHolsterIntentActive(1.0) { return 0; };
  let meleeAttack = weapon.VRGetMeleeAttack();
  if !IsDefined(meleeAttack) { return 0; };

  // blade segment = grip + direction * blade length. The game's attack Range() includes a lunge
  // (2-3 m) which makes the segment long enough to clip NEIGHBOURING NPCs ("I cut one, another gets
  // hit"); cap to ~physical blade length so only what the blade actually touches is hit.
  let reach = weapon.VRMeleeReach();
  if reach > 1.25 { reach = 1.25; };
  let dir = Vector4.Normalize(bladeDir);
  let bladeEnd = new Vector4(bladeStart.X + dir.X*reach, bladeStart.Y + dir.Y*reach, bladeStart.Z + dir.Z*reach, 1.0);

  return this.VRMeleeSweepHit(weapon, bladeStart, bladeEnd, bladeStart, bladeEnd, hitRadius, passive);
}

@addMethod(PlayerPuppet)
public func VRMeleeSweepHit(weapon: wref<WeaponObject>, bladeStart: Vector4, bladeEnd: Vector4, previousStart: Vector4, previousEnd: Vector4, hitRadius: Float, passive: Bool) -> Int32 {
  let steps: Int32;
  let step: Int32;
  let alpha: Float;
  let a: Vector4;
  let b: Vector4;
  let closest: Vector4;
  let contact: Vector4;
  let base: Vector4;
  let checked: array<EntityID>;
  if !IsDefined(weapon) || this.VRHolsterIntentActive(1.00) { return 0; };
  steps = Clamp(CeilF(MaxF(Vector4.Distance(bladeStart, previousStart), Vector4.Distance(bladeEnd, previousEnd)) / 0.04), 1, 16);
  // Candidates around the entire previous-to-current blade sweep.
  let q: TargetSearchQuery;
  q.testedSet = TargetingSet.Complete;
  q.searchFilter = TSF_NPC();
  q.maxDistance = MaxF(Vector4.Distance(this.GetWorldPosition(), bladeEnd), Vector4.Distance(this.GetWorldPosition(), previousEnd)) + hitRadius + 1.80;
  q.filterObjectByDistance = true;
  q.ignoreInstigator = true;
  let parts: array<TS_TargetPartInfo>;
  GameInstance.GetTargetingSystem(this.GetGame()).GetTargetParts(this, q, parts);

  // closest enemy whose body is within hitRadius of the blade segment
  let bestEnemy: wref<GameObject>;
  let bestPos: Vector4;
  let bestDist = 99999.0;
  let i = 0;
  while i < ArraySize(parts) {
    let comp = TS_TargetPartInfo.GetComponent(parts[i]);
    if IsDefined(comp) {
      let ent = comp.GetEntity() as GameObject;
      if IsDefined(ent) && !ent.IsDead() && !ArrayContains(checked, ent.GetEntityID()) {
        ArrayPush(checked, ent.GetEntityID());
        base = ent.GetWorldPosition();
        step = 0;
        while step <= steps {
          alpha = Cast<Float>(step) / Cast<Float>(steps);
          a = previousStart + (bladeStart - previousStart) * alpha;
          b = previousEnd + (bladeEnd - previousEnd) * alpha;
          // Continuous body axis avoids the gaps between five fixed heights.
          closest = VrCyberwareContactMath.ClosestBody(a, b, base);
          if closest.W <= hitRadius && closest.W < bestDist {
            bestDist = closest.W;
            bestEnemy = ent;
            contact = closest; contact.W = 1.00;
            bestPos = new Vector4(base.X, base.Y, ClampF(closest.Z, base.Z + 0.25, base.Z + 1.65), 1.00);
          };
          step += 1;
        };
      }
    }
    i += 1;
  }
  // Deduplicate within a deliberate stroke; native damage/bonuses stay intact.
  return this.VRCyberwareOrdinaryContact(weapon, bestEnemy, contact, bestPos, passive);
}

// SWING WHOOSH (VR). In the flat game the whoosh rides on the attack ANIMATION's audio events,
// resolved through the weapon's cooked audio metadata (audioMeleeSettings: normalWhoosh /
// fastWhoosh / slowWhoosh event lists per weapon family) — a VR swing plays no attack anim, so
// swings are dead silent. Scripts can't read cooked audio metadata, so this replicates the SAME
// selection with the exact event names from the game's audio configs (dumped from
// base\sound\metadata: audio_melee_metadata_* for every melee family + cyberware banks):
// family = weapon ItemType, variant = fast/normal by real swing speed, heavy on strong swings.
// Played positionally on the WEAPON object — same audio language as the vanilla whoosh
// (weapon emitter), and the same "main whoosh + detail layer" pairing the configs use.
@addMethod(PlayerPuppet)
public func VRMeleeWhoosh(weapon: wref<WeaponObject>, fast: Bool, strong: Bool) -> Void {
  if !IsDefined(weapon) { return; };
  let rec = weapon.GetWeaponRecord();
  if !IsDefined(rec) { return; };
  let t = rec.ItemType().Type();
  let a: CName = n"";  // main whoosh
  let b: CName = n"";  // detail layer (knife ring / tomahawk head / sledge ring), as in the configs
  switch t {
    case gamedataItemType.Wea_Katana:
    case gamedataItemType.Wea_Sword:
    case gamedataItemType.Wea_LongBlade:
      a = n"w_melee_katana_whoosh";                                        // katana config: normalWhoosh only
      break;
    case gamedataItemType.Wea_ShortBlade:
    case gamedataItemType.Wea_Knife:
    case gamedataItemType.Wea_Machete:
      a = fast ? n"w_melee_blade_whoosh_fast" : n"w_melee_blade_whoosh_normal";
      b = n"w_melee_detail_knife_ring";
      break;
    case gamedataItemType.Wea_Axe:
      a = fast ? n"w_melee_blade_whoosh_fast" : n"w_melee_blade_whoosh_normal";
      b = n"w_melee_detail_tomahawk";                                      // tomahawk/vb_axe configs
      break;
    case gamedataItemType.Wea_Chainsword:
      a = fast ? n"w_melee_blade_whoosh_fast" : n"w_melee_blade_whoosh_normal";
      b = fast ? n"w_melee_cut_o_matic_whoosh_fast" : n"w_melee_cut_o_matic_whoosh";
      break;
    case gamedataItemType.Wea_Hammer:
      a = fast ? n"w_melee_sledgehammer_whoosh_fast" : n"w_melee_sledgehammer_whoosh";
      b = fast ? n"w_melee_sledgehammer_detail_ring" : n"w_melee_sledgehammer_detail_ring_sharp";
      break;
    case gamedataItemType.Wea_OneHandedClub:
    case gamedataItemType.Wea_TwoHandedClub:
    case gamedataItemType.Wea_Melee:
      if strong { a = n"w_melee_blunt_whoosh_heavy"; }
      else { a = fast ? n"w_melee_blunt_whoosh_fast" : n"w_melee_blunt_whoosh_low"; };
      break;
    case gamedataItemType.Cyb_MantisBlades:
      if strong { a = n"w_cyb_mantis_whoosh_heavy"; }
      else { a = fast ? n"w_cyb_mantis_whoosh_fast" : n"w_cyb_mantis_whoosh"; };
      b = n"w_cyb_mantis_whoosh_ring";
      break;
    case gamedataItemType.Cyb_NanoWires:
      a = fast ? n"w_cyb_nano_wire_whoosh_swirl" : n"w_cyb_nano_wire_whoosh";
      break;
    case gamedataItemType.Cyb_StrongArms:
    case gamedataItemType.Wea_Fists:
      a = fast ? n"w_melee_fist_whoosh_fast" : n"w_melee_fist_whoosh_heavy";  // fists config naming
      break;
    default:
      a = fast ? n"w_melee_fist_whoosh_fast" : n"w_melee_fist_whoosh_heavy";  // neutral air whoosh
      break;
  }
  if NotEquals(a, n"") { GameObject.PlaySoundEvent(weapon, a); };
  if NotEquals(b, n"") { GameObject.PlaySoundEvent(weapon, b); };
}

// WEAPON DRAW SOUND (VR). Draw/equip sounds ride on the draw ANIMATION's audio events, which the
// VR rig never plays — so every draw is silent. Same replication approach as the whoosh:
//   melee — the equipSound lists from audio_melee_metadata_* per family (incl. detail layers);
//   guns  — audio family names live only in cooked entity data (scripts can't read it, TweakDB
//           has none of them), so the full set of player '*_equip' events from the sound banks
//           is embedded and matched by the gun's name token from its TweakDB id
//           ("Items.Preset_Lexington_Wilson" -> "lexington" -> w_gun_pistol_power_lexington_equip);
//           unmatched guns degrade to the game's own generic draws by weight class
//           (w_gun_npc_equip_light/medium/heavy — what NPC draws play).
// Played positionally on the weapon entity, like the whoosh and the hit feedback.
@addMethod(PlayerPuppet)
public func VREquipSound(weapon: wref<WeaponObject>) -> Void {
  if !IsDefined(weapon) { return; };
  let rec = weapon.GetWeaponRecord();
  if !IsDefined(rec) { return; };
  let t = rec.ItemType().Type();
  let a: CName = n"";
  let b: CName = n"";
  switch t {
    case gamedataItemType.Wea_Katana:
    case gamedataItemType.Wea_Sword:
    case gamedataItemType.Wea_LongBlade:
      a = n"w_melee_katana_equip";
      break;
    case gamedataItemType.Wea_ShortBlade:
    case gamedataItemType.Wea_Knife:
      a = n"w_melee_knife_additional_equip";
      b = n"w_melee_detail_knife_ring";
      break;
    case gamedataItemType.Wea_Machete:
      a = n"w_melee_knife_additional_equip";
      break;
    case gamedataItemType.Wea_Axe:
      a = n"w_melee_detail_tomahawk";
      b = n"w_melee_knife_additional_equip";
      break;
    case gamedataItemType.Wea_Chainsword:
      a = n"w_melee_cut_o_matic_equip";
      break;
    case gamedataItemType.Wea_Hammer:
      break;                                        // sledgehammer config: equipSound "None"
    case gamedataItemType.Wea_OneHandedClub:
    case gamedataItemType.Wea_TwoHandedClub:
    case gamedataItemType.Wea_Melee:
      a = n"w_melee_detail_bat_equip";
      break;
    case gamedataItemType.Wea_Fists:
      a = n"lcm_mvs_leather_jacket_fast_light";     // fists config: cloth swish
      break;
    case gamedataItemType.Cyb_MantisBlades:
      a = n"w_cyb_mantis_equip";
      break;
    case gamedataItemType.Cyb_StrongArms:
      a = n"w_cyb_strongarms_equip";
      break;
    case gamedataItemType.Cyb_NanoWires:
      a = n"w_cyb_nano_wire_out";                   // wire deploy
      break;
    default:
      break;
  }
  if NotEquals(a, n"") || NotEquals(b, n"") {
    if NotEquals(a, n"") { GameObject.PlaySoundEvent(weapon, a); };
    if NotEquals(b, n"") { GameObject.PlaySoundEvent(weapon, b); };
    return;
  };
  if Equals(t, gamedataItemType.Wea_Hammer) { return; };

  // guns: match the item-id name token against the sound banks' player equip events
  let idStr = StrLower(TDBID.ToStringDEBUG(ItemID.GetTDBID(weapon.GetItemID())));
  let parts = StrSplit(idStr, "_");
  if ArraySize(parts) >= 2 {
    let name = parts[1];
    let evts: array<String> = [
      "w_gun_pistol_power_lexington_equip", "w_gun_pistol_power_liberty_equip",
      "w_gun_pistol_power_unity_equip", "w_gun_pistol_power_nue_equip",
      "w_gun_pistol_power_slaughtomatic_equip", "w_gun_pistol_power_silverhand_equip",
      "w_gun_pistol_tech_omaha_equip", "w_gun_pistol_tech_kenshin_equip",
      "w_gun_grit_equip", "w_gun_ticon_equip", "w_gun_pistol_toygun_equip",
      "w_gun_pistol_smart_chao_equip", "w_gun_pistol_smart_kappa_equip",
      "w_gun_pistol_smart_yukimura_equip",
      "w_gun_revol_power_overture_equip", "w_gun_revol_power_crusher_equip",
      "w_gun_revol_power_nova_equip", "w_gun_metel_equip",
      "w_gun_revol_tech_burya_equip", "w_gun_revol_tech_quasar_equip",
      "w_gun_smg_power_saratoga_equip", "w_gun_smg_power_guillotine_equip",
      "w_gun_smg_power_pulsar_equip", "w_gun_borg4a_equip", "w_gun_warden_equip",
      "w_gun_smg_tech_senkoh_equip", "w_gun_smg_smart_shingen_equip",
      "w_gun_smg_smart_sidewinder_equip", "w_gun_smg_smart_dian_equip",
      "w_gun_rifle_power_ajax_equip", "w_gun_rifle_power_copperhead_equip",
      "w_gun_rifle_power_masamune_equip", "w_gun_rifle_power_nowaki_equip",
      "w_gun_rifle_power_pozhar_equip", "w_gun_rifle_power_sor22_equip",
      "w_gun_rifle_power_kolac_equip", "w_gun_rifle_tech_achilles_equip",
      "w_gun_shotgun_power_carnage_equip", "w_gun_shotgun_power_tactician_equip",
      "w_gun_shotgun_power_igla_equip", "w_gun_shotgun_power_testera_equip",
      "w_gun_shotgun_tech_satara_equip", "w_gun_shotgun_smart_zhuo_equip",
      "w_gun_shotgun_smart_palica_equip",
      "w_gun_sniper_power_grad_equip", "w_gun_osprey_equip",
      "w_gun_sniper_tech_rasetsu_equip", "w_gun_sniper_tech_nekomata_equip",
      "w_gun_sniper_smart_ashura_equip",
      "w_gun_lmg_power_ma70_equip", "w_gun_lmg_power_defender_equip",
      "w_gun_hmg_militech_equip", "w_gun_hercules3ax_equip"
    ];
    let i = 0;
    while i < ArraySize(evts) {
      if StrContains(evts[i], "_" + name) {
        GameObject.PlaySoundEvent(weapon, StringToName(evts[i]));
        return;
      };
      i += 1;
    };
  };

  // no bank event matched: the game's generic draw foley by weight class
  switch t {
    case gamedataItemType.Wea_Handgun:
    case gamedataItemType.Wea_Revolver:
    case gamedataItemType.Wea_SubmachineGun:
      a = n"w_gun_npc_equip_light";
      break;
    case gamedataItemType.Wea_SniperRifle:
    case gamedataItemType.Wea_LightMachineGun:
    case gamedataItemType.Wea_HeavyMachineGun:
      a = n"w_gun_npc_equip_heavy";
      break;
    default:
      a = n"w_gun_npc_equip_medium";
      break;
  }
  GameObject.PlaySoundEvent(weapon, a);
}

// NO IDLE AUTO-UNEQUIP FOR MELEE. The melee PSM's PublicSafe state arms a 15s timer
// (timeToAutoUnequipWeapon in public zones / timeToUnequipMeleeware for arm cyberware) and its
// OnTick sends UnequipWeapon when it expires. The timer resets only on ATTACK INPUT — which a VR
// swing never presses, so the blade kept holstering itself mid-use ("idle" by the game's book
// while the player is physically swinging it). Guns don't idle-unequip; give melee the same
// behavior by neutralizing the timer right before the vanilla check reads it.
@wrapMethod(MeleePublicSafeEvents)
protected func OnTick(timeDelta: Float, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  this.m_unequipTime = -1.0;
  wrappedMethod(timeDelta, stateContext, scriptInterface);
}
