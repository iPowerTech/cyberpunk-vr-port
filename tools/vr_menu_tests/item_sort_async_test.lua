package.preload["modules/ink"]=function() return {} end
package.preload["modules/inventory_data"]=function() return {} end
CName={new=function(v) return v end}
Game={GetScriptableSystemsContainer=function() return {Get=function() return {} end} end}
WrappedInventoryItemData={new=function() return {} end}
BackpackDataView={new=function() return {SortItemNew=function(s,a,b)
  local field=s.itemSortMode:find("Name") and "name" or "value"
  local av,bv=a.Item[field],b.Item[field]
  if av==bv then return false end
  if s.itemSortMode:find("Desc") then return av>bv end
  return av<bv
end} end}
local S=dofile("mods/cet/CyberpunkVRPort_Menu/modules/item_sort.lua")
S.options=function() local r={};for _,key in ipairs({"QualityAsc","NameAsc","NameDesc","PriceAsc","PriceDesc"}) do r[#r+1]={key=key,mode=key} end;return r end
for _,count in ipairs({0,1,2,65,513}) do
  local items={};for i=1,count do local value=i*37%17;items[i]={key=string.format("%04d",i),price=value,ui={value=value,name=tostring(value)}} end
  for _,mode in ipairs(S.options()) do
    local expected=S.apply(items,mode.key);local task=S.begin(items,mode.key);local got;local steps=0
    repeat got=S.step(task,os.clock());steps=steps+1;assert(steps<20000) until got
    assert(#got==#expected)
    for i=1,#got do assert(got[i].key==expected[i].key,"async order differs: "..mode.key) end
    if count>1 then assert(steps>count,"work must yield across updates") end
  end
end
print("PASS: bounded incremental sort matches native-comparator ordering, ties and contextual prices")
local nativeCalls=0
UnicodeStringCompare=function(a,b) return a==b and 0 or a<b and -1 or 1 end
BackpackDataView.new=function() return {SortItemNew=function(_,a,b)
  nativeCalls=nativeCalls+1;local x,y=a.Item,b.Item
  if x.favorite~=y.favorite then return x.favorite end
  if x.quality~=y.quality then return x.quality>y.quality end
  return UnicodeStringCompare(x.name,y.name)<0
end} end
local items={}
for i=1,173 do
  local ui={name=tostring(i%9),favorite=i%11==0,quality=i%5,
    GetName=function(s) return s.name end,GetQualityInt=function(s) return s.quality end,
    IsPlayerFavourite=function(s) return s.favorite end}
  items[i]={key=string.format("%04d",i),ui=ui}
end
local expected=S.apply(items,"QualityAsc");nativeCalls=0
local task=S.begin(items,"QualityAsc");local result
repeat result=S.step(task,os.clock()) until result
for i=1,#result do assert(result[i].key==expected[i].key) end
assert(nativeCalls==0,"quality sorting must use cached keys, not allocate native pair builders")
print("PASS: cached quality order preserves favourites, names and deterministic ties")
