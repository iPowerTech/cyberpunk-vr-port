// CyberpunkVRPort — VR HUD, Phase 1: safe-zone scale + recenter.
//
// The VR pipeline (dxgi.dll) copies the full game frame into a ~104 deg per-eye
// projection, which pushes the flat-screen HUD (minimap / health / ammo) to the
// periphery where it is uncomfortable or clipped. We walk up from a scripted HUD
// controller to the HUD layer root canvas (named "Root") and scale it around
// screen center, shrinking the WHOLE HUD inward into the comfortable VR view.
//
// The HUD layer controller (gameuiRootHudGameController) is native-only and
// cannot be wrapped, so we trigger from scripted HUD controllers and reach the
// shared root via the Codeware-exposed inkWidget.parentWidget chain.
//
// Pure ink-side: no dxgi.dll changes. Tune VrHudScale() to taste (1.0 = vanilla).

module CyberpunkVRPort.Hud

@if(ModuleExists("Codeware"))
public func VrHudScale() -> Float {
  let scale = VrHudScaleConfig();
  if scale < 0.25 {
    return 0.25;
  }
  if scale > 1.00 {
    return 1.00;
  }
  return scale;
}

@if(ModuleExists("Codeware"))
func VrTryApplyScale(widget: ref<inkWidget>) -> Bool {
  if !IsDefined(widget) {
    return false;
  }
  let s = VrHudScale();
  widget.SetAnchorPoint(new Vector2(0.5, 0.5));
  widget.SetScale(new Vector2(s, s));
  return true;
}

// First try the real shared HUD root through inkHUDLayer. This affects the whole
// HUD at once: minimap, quest tracker, left-bottom quick actions, right-side
// hints, etc. If that lookup fails, fall back to the local parent chain.
@if(ModuleExists("Codeware"))
func VrApplyHudSafeZone(start: ref<inkWidget>) -> Void {
  let inkSystem = GameInstance.GetInkSystem();
  if IsDefined(inkSystem) {
    let layer = inkSystem.GetLayer(n"inkHUDLayer");
    if IsDefined(layer) {
      let window = layer.GetVirtualWindow();
      if IsDefined(window) {
        let root = window.GetWidgetByPathName(n"Root");
        if VrTryApplyScale(root) {
          return;
        }
      }
    }
  }

  // Fallback for cases where the layer root is not available yet at hook time.
  let cur: wref<inkWidget> = start;
  let i = 0;
  while IsDefined(cur) && i < 32 {
    if Equals(cur.GetName(), n"Root") {
      VrTryApplyScale(cur);
      return;
    }
    cur = cur.GetParentWidget();
    i += 1;
  }
}

// Trigger: QuestTrackerGameController is a scripted HUD controller that
// initializes once the HUD layer is built, so its widget tree (and the shared
// "Root" ancestor) exists by then.
@if(ModuleExists("Codeware"))
@wrapMethod(QuestTrackerGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  VrApplyHudSafeZone(this.GetRootCompoundWidget());
  return result;
}

// Redundant trigger for cases where the minimap initializes first or the quest
// tracker is hidden by gameplay state or another mod. Re-applying the absolute
// scale is safe.
@if(ModuleExists("Codeware"))
@wrapMethod(MinimapContainerController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  VrApplyHudSafeZone(this.GetRootCompoundWidget());
  return result;
}
