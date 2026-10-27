local D=dofile("mods/cet/CyberpunkVRPort_Menu/modules/load_data.lua")
IsEP1=function() return false end
inkImageWidget={new=function() return {} end}
local function info(index,id,group,stamp,level,time)
  return {saveIndex=index,saveID=id,playthroughID=group,timestamp=stamp,level=level,
    lifePath="Nomad",playTime=time,playthroughTime=time,isValid=true,IsEp1Save=function() return false end}
end
local a=info(0,10,"one",200,40,1000)
local b=info(1,20,"one",100,50,2000)
local other=info(2,30,"two",150,5,300)
local groups=D.groups({total=3,records={[0]=a,[1]=b,[2]=other}})
assert(#groups==2 and groups[1].id=="one" and #groups[1].saves==2)
assert(groups[1].saves[1]==a and groups[1].seconds==1000 and groups[1].level==40,"header must follow latest save, as in the native menu")
local calls=0
local c={loading=false,records={[0]=a},names={"AutoSave-1"},revision=0,
  handler={RequestSavesForLoad=function() calls=calls+1 end,RequestSavedGameScreenshot=function() calls=calls+1 end}}
assert(D.beginLoad(c,a) and not D.beginLoad(c,a),"only one load may be pending")
D.names(c,{"AutoSave-new","AutoSave-1"})
assert(c.pending.index==1,"refresh must find the same file after indices shift")
D.metadata(c,info(1,10,"one",200,40,1000))
assert(c.readyLoad==1 and calls==2)
local changed={pending={file="AutoSave-1",id=10,stamp="200",index=0},revision=0}
D.metadata(changed,info(0,99,"one",201,41,1100))
assert(changed.error=="changed" and not changed.readyLoad,"overwritten slots must not load")
local missing={pending={file="deleted",id=10,stamp="200"},revision=0}
D.names(missing,{"AutoSave-1"})
assert(missing.error=="changed" and not missing.readyLoad)
a.isValid=false;assert(not D.available(a));a.isValid=true
a.IsEp1Save=function() return true end;assert(not D.available(a),"missing expansion must block loading")
print("PASS: native grouping, reordered slots, duplicate dispatch, stale saves and compatibility")
