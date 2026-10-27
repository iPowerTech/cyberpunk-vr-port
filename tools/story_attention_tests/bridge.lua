-- Execute with Lua 5.1/LuaJIT (CET's semantics), passing the real module path.
local source=assert(arg[1])
local state={menu=false,player=true,started=0,done=0,loco=0,tier=1,vehicle=0,workspot=false,
             choices=false,request=0,index=-1,inspected=0,broken=false}
local defs={UI_System={IsInMenu='menu'},PlayerStateMachine={Locomotion='loco',SceneTier='tier',Vehicle='vehicle'},
            UIInteractions={InteractionChoiceHub='hub'}}
local player={GetEntityID=function()return {hash=1}end}
local object={CyberpunkVRPortManualFocusClueIndex=function()return state.index end,
              CyberpunkVRPortInspectManualFocusClue=function(_,index)
                  assert(index==state.index and index>=0);state.inspected=state.inspected+1;return true
              end}
Game={GetPlayer=function()return state.player and player or nil end,
      GetBlackboardSystem=function()
          if state.broken then error('save transition')end
          return {Get=function(_,d)
              if d==defs.UI_System then return {GetBool=function()return state.menu end}end
              return {GetVariant=function()return {choices=state.choices and {1} or {}}end}
          end,GetLocalInstanced=function()return {GetInt=function(_,field)return state[field]end}end}
      end,
      GetWorkspotSystem=function()return {IsActorInWorkspot=function()return state.workspot end}end,
      GetQuestsSystem=function()return {GetFactStr=function(_,name)
          if name=='q110_02_camera_scan_start' then return state.started end
          assert(name=='q110_02_scanning_done');return state.done
      end}end,
      GetTargetingSystem=function()return {GetLookAtObject=function()return object end}end}
function GetAllBlackboardDefs()return defs end
function FromVariant(v)return v end
local updates,clues={},{}
function VRStoryAttentionUpdate(...)updates[#updates+1]={...};return 0 end
function VRManualClueUpdate(p,o)clues[#clues+1]={p,o};return state.request end
local module=assert(loadfile(source))()
local checks=0
local function check(value,message)checks=checks+1;assert(value,message)end
local function tick(open)module.tick(.05,open);return updates[#updates]end
local call=tick(false)
check(call[1]==player and call[2]==0 and call[3]==0 and call[9]==0,'ordinary context')
state.started=1;call=tick(false)
check(call[2]==1 and call[3]==0,'quest facts forwarded exactly')
state.done=1;call=tick(false)
check(call[3]==1,'completion not latched')
state.loco=9;state.tier=3;state.workspot=true;call=tick(false)
check(call[4]==9 and call[5]==3 and call[6]==1,'workspot inputs')
state.vehicle=3;state.choices=true;call=tick(false)
check(call[7]==3 and call[8]==1,'passenger hub')
state.choices=false;call=tick(false)
check(call[8]==0,'hub closes')
state.index=2;state.request=0;tick(false)
check(state.inspected==0 and clues[#clues][2]==object,'scanned clue does not auto-inspect')
state.request=1;tick(false)
check(state.inspected==1,'explicit request inspects')
state.request=0;tick(false)
check(state.inspected==1,'held request does not repeat')
state.index=-1;state.request=1;tick(false)
check(state.inspected==1 and clues[#clues][2]==nil,'ineligible clue cannot inspect')
state.menu=true;state.index=2;call=tick(false)
check(call[1]==nil and call[9]==1 and clues[#clues][2]==nil,'menu releases both claims')
state.menu=false;call=tick(true)
check(call[1]==nil and call[9]==1,'VR overlay releases claims')
state.player=false;call=tick(false)
check(call[1]==nil and call[9]==1,'missing player clears context')
state.player=true;state.broken=true;call=tick(false)
check(call[1]==nil and call[9]==1 and module.error~=nil,'transition error fails closed')
state.broken=false;state.request=0;call=tick(false)
check(call[1]==player and module.error==nil,'same EntityID after load is re-evaluated')
module.shutdown();call=updates[#updates]
check(call[1]==nil and call[9]==1 and clues[#clues][2]==nil,'shutdown clears leases')
print('PASS '..checks..' production Lua context/input bridge checks')
