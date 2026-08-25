// CyberpunkVRPort — hide settings rows that produce artefacts in the VR view.
//
// WHY NOT THE OTHER TWO ROUTES, both checked first:
//
//   * There is nothing to delete from the archive. Every game archive searched for *settings*
//     returns menu chrome only (settings_main.inkwidget, settings.inkstyle, controller atlases).
//     The options are config vars, not assets.
//   * r6/config/settings/options.json DOES carry a per-option `is_visible`, and 37 of the game's
//     own options ship hidden that way (Gamma, Saturation, Brightness, HudScaling...). But the
//     /graphics/* subgroups have NO option blocks in that file at all -- every graphics option is
//     registered by the engine and declared nowhere on disk. Declaring one there by hand CRASHES
//     THE GAME ON LAUNCH: a string_list with no `values` is fatal, and the real value list is
//     engine-side and not written down anywhere, so there is nothing to copy.
//
// So the row is removed where the menu is built, from script, which cannot affect startup at all.
//
// WHERE THIS HOOKS, and why this method and not another. SettingsMainGameController.AddSettingsGroup
// fills SettingsCategory.options from ConfigGroup.GetVars(...), and PopulateSettingsData() is the
// one call that builds the whole m_data array out of those groups. Wrapping it and filtering
// afterwards needs no knowledge of how the engine registered anything.
//
// It filters `options` ONLY -- it never drops a category or a subcategory. PopulateCategories walks
// m_data by index and pushes non-empty ones into m_menusList, while OnMenuChanged maps the selector
// index straight back into m_data[index]. Removing an entry would shift one of those and not the
// other. Same length, same order, fewer rows inside.
//
// Hiding is not enforcing: a player can still edit UserSettings.json by hand. If the value has to
// hold no matter what, that is a separate change on the plugin side.

module CyberpunkVRPort.SettingsGuard

// The rows to take out, matched on ConfigVar.GetName(). Scoped by group path as well, so a
// same-named var in some other group is left alone.
//
// CascadedShadowsRange / CascadedShadowsResolution: the cascade shadow atlas is shared between the
// two views this port renders, and a view that rasterises it from its own frustum is this port's
// known artefact source -- see docs and the cascade-agreement notes. Both ship at Low here because
// that is the setting the artefact does not appear at.
public func VRPortHiddenSettingGroup() -> CName {
  return n"/graphics/advanced";
}

public func VRPortIsHiddenSetting(v: ref<ConfigVar>) -> Bool {
  if !IsDefined(v) { return false; }
  if NotEquals(v.GetGroupPath(), VRPortHiddenSettingGroup()) { return false; }
  let n: CName = v.GetName();
  return Equals(n, n"CascadedShadowsRange") || Equals(n, n"CascadedShadowsResolution");
}

// Returns the list with the hidden vars removed. Rebuilt rather than mutated in place: the source
// is an array field inside a struct held in an array, and copying it out is the only assignment
// that is unambiguous.
public func VRPortFilterSettingVars(src: array<ref<ConfigVar>>) -> array<ref<ConfigVar>> {
  let out: array<ref<ConfigVar>>;
  let i: Int32 = 0;
  while i < ArraySize(src) {
    if !VRPortIsHiddenSetting(src[i]) {
      ArrayPush(out, src[i]);
    };
    i += 1;
  };
  return out;
}

@wrapMethod(SettingsMainGameController)
private final func PopulateSettingsData() -> Void {
  wrappedMethod();

  let rebuilt: array<SettingsCategory>;
  let i: Int32 = 0;
  while i < ArraySize(this.m_data) {
    let cat: SettingsCategory = this.m_data[i];
    cat.options = VRPortFilterSettingVars(cat.options);

    let subs: array<SettingsCategory>;
    let j: Int32 = 0;
    while j < ArraySize(cat.subcategories) {
      let sub: SettingsCategory = cat.subcategories[j];
      sub.options = VRPortFilterSettingVars(sub.options);
      // Kept even when it empties out, for the index reason in the header note. An empty
      // subcategory renders as a heading with nothing under it, which the game already handles --
      // and neither of the two rows removed here is the only thing in its subcategory.
      ArrayPush(subs, sub);
      j += 1;
    };
    cat.subcategories = subs;

    ArrayPush(rebuilt, cat);
    i += 1;
  };
  this.m_data = rebuilt;
}
