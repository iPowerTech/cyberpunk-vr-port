-- The existing inventory scene owns the customized puppet and mirrors native equipment events.
-- Sampling its live ink atlas keeps the preview inside the world menu and never changes the HMD camera.
local K=require("modules/ink")
local Pref=require("modules/preferences")
local P={}
local function image(parent,x,y,w,h,atlas,part,aspect,uv,color)
  local ratio=aspect*(uv[3]-uv[1])/(uv[4]-uv[2])
  local width=math.min(w,h*ratio);local height=width/ratio
  local viewport=K.canvas(parent,x,y,w,h)
  local mask=inkScrollAreaWidget.new()
  mask:SetAnchor(inkEAnchor.TopLeft);mask:SetAnchorPoint(Vector2.new({X=0,Y=0}))
  mask:SetSize(Vector2.new({X=w,Y=h}));mask:SetFitToContent(false);mask:SetUseInternalMask(true)
  mask.constrainContentPosition=false;mask.fitToContentDirection=inkFitToContentDirection.None;mask:Reparent(viewport)
  local content=K.canvas(mask,0,0,w,h)
  local fullW,fullH=width/(uv[3]-uv[1]),height/(uv[4]-uv[2])
  return K.image(content,(w-width)/2-uv[1]*fullW,(h-height)/2-uv[2]*fullH,fullW,fullH,atlas,part,color,1)
end
local function weapon(puppet,id,refresh)
  local ts=Game.GetTransactionSystem();local slot=TweakDBID.new("AttachmentSlots.WeaponRight")
  local object=ts:GetItemInSlot(puppet,slot)
  local previous=object and object:GetItemID()
  local preview=ItemID.IsValid(id) and ts:CreatePreviewItemID(id) or ItemID.None()
  if previous and previous==preview and not refresh then return end
  if previous then
    gameuiInGameMenuGameController.SetAnimWrapperBasedOnItemFriendlyName(puppet,previous,0)
    ts:RemoveItemFromSlot(puppet,slot,true);ts:RemoveItem(puppet,previous,1)
  end
  if not ItemID.IsValid(id) then return end
  local source=ts:GetItemData(Game.GetPlayer(),id)
  if not source then return end
  if not ts:HasItem(puppet,preview) then ts:GivePreviewItemByItemData(puppet,source) end
  ts:AddItemToSlot(puppet,slot,preview)
  gameuiInGameMenuGameController.SetAnimWrapperBasedOnItemFriendlyName(puppet,preview,1)
end
function P.update(kit)
  if not Pref.characterModel then return end
  local state=kit.S.gameplay
  if not state or not state.inventoryPuppet then return end
  if kit.S.time-(state.inventoryPreviewAt or 0)<.2 and not state.inventoryPreviewDirty then return end
  state.inventoryPreviewAt=kit.S.time
  local slot=state.inventory and state.inventory.weaponSlot or 0
  local id=EquipmentSystem.GetData(Game.GetPlayer()):GetItemInEquipSlot(gamedataEquipmentArea.Weapon,slot)
  weapon(state.inventoryPuppet,id,state.inventoryPreviewDirty)
  state.inventoryPreviewDirty=nil
end
-- A static native bitmap: no preview puppet, dynamic texture, forced animation or per-frame work.
local function static(kit,parent,x,y,w,h,area)
  local state=kit.S.gameplay
  local female=Game.GetPlayer():GetResolvedGenderName()==CName.new("Female")
  local uv={0,0,1,1}
  if area=="Head" or area=="Face" then uv={.18,0,.82,.42}
  elseif area=="OuterChest" or area=="InnerChest" then uv={.05,.12,.95,.68}
  elseif area=="Legs" then uv={.15,.46,.85,1}
  elseif area=="Feet" then uv={.2,.76,.8,1} end
  state.inventoryPreview=image(parent,x,y,w,h,
    female and [[base\gameplay\gui\fullscreen\ripperdoc\woman_body.inkatlas]] or [[base\gameplay\gui\fullscreen\ripperdoc\man_body.inkatlas]],
    female and "wo_intergumentary" or "ma_intergumentary",female and (735/1781) or (1089/1861),uv,K.CYAN)
  state.inventoryPreviewMode="static";state.inventoryPreviewArea=area;state.inventoryPreviewFemale=female
  return true
end
-- Always draws something: while the live puppet is not there yet, the static figure stands in (the next
-- redraw switches to the live one), so the panel never shows a loading line.
function P.show(kit,parent,x,y,w,h,area)
  local state=kit.S.gameplay
  if not Pref.characterModel then
    P.release(kit)
    return static(kit,parent,x,y,w,h,area)
  end
  local ctrl=kit.menuController and kit.menuController()
  local puppet=ctrl and ctrl:GetPuppet(CName.new("inventory"))
  if not puppet then return static(kit,parent,x,y,w,h,area) end
  if state.inventoryPuppet~=puppet then
    P.release(kit)
    state.inventoryPuppet=puppet
    Game.GetAnimationSystem():SetForcedVisible(puppet:GetEntityID(),true)
    local feature=AnimFeature_Paperdoll.new();feature.inventoryScreen=true
    AnimationControllerComponent.ApplyFeature(puppet,CName.new("Paperdoll"),feature)
  end
  if not state.inventoryPreviewSource then
    -- On a fresh session the atlas alone has no producer. The native previewPanel binds its
    -- scene/camera to preview.dtex; retain that binding only while the live preview is enabled.
    local source=ctrl:SpawnFromExternal(kit.S.layerCol,ResRef.FromString([[base\gameplay\gui\fullscreen\inventory\inventory_screen.inkwidget]]),CName.new("previewPanel"))
    if not source then return static(kit,parent,x,y,w,h,area) end
    source:SetOpacity(0);source:SetScale(Vector2.new({X=.001,Y=.001}))
    state.inventoryPreviewSource=source;state.inventoryPreviewSourceParent=kit.S.layerCol
  end
  P.update(kit)
  local uv={.20,.04,.85,1}
  if area=="Head" or area=="Face" then uv={.34,.08,.64,.525}
  elseif area=="OuterChest" or area=="InnerChest" then uv={.27,.10,.80,.72}
  elseif area=="Legs" then uv={.30,.55,.68,1}
  elseif area=="Feet" then uv={.35,.78,.66,1} end
  -- The native previewPanel displays the target at 1900 x 1800. Preserve that mapping while cropping.
  state.inventoryPreview=image(parent,x,y,w,h,[[base\gameplay\gui\fullscreen\inventory\preview.inkatlas]],"preview",1900/1800,uv,K.WHITE)
  state.inventoryPreviewMode="live";state.inventoryPreviewArea=area
  return true
end
function P.release(kit)
  local state=kit.S.gameplay
  if state and state.inventoryPuppet then
    -- Return the shared puppet to the game's current visual loadout before relinquishing it.
    pcall(function() weapon(state.inventoryPuppet,EquipmentSystem.GetData(Game.GetPlayer()):GetVisualItemInSlot(gamedataEquipmentArea.Weapon),false) end)
    pcall(function() Game.GetAnimationSystem():SetForcedVisible(state.inventoryPuppet:GetEntityID(),false) end)
    state.inventoryPuppet=nil;state.inventoryPreviewAt=nil
  end
  if state then
    if state.inventoryPreviewSource and state.inventoryPreviewSourceParent then
      pcall(function() state.inventoryPreviewSourceParent:RemoveChild(state.inventoryPreviewSource) end)
    end
    state.inventoryPreviewSource=nil;state.inventoryPreviewSourceParent=nil
    state.inventoryPreview=nil;state.inventoryPreviewMode=nil
  end
end
return P
