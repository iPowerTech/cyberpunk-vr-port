local C=dofile('mods/cet/CyberpunkVRPort_Stereo/modules/computer_touch.lua')
local calls={};local sleeping=true;local blocked=false;local powered=true
inkWidgetRef={Get=function(w)return w end}
EComputerMenuType={MAIN=0};EDocumentType={MAIL=1}
GoToMenuEvent={new=function()return {} end}
local widget={IsInteractive=function()return true end}
local layout={screenSaverSlot=widget,HideWindow=function()calls.close=true end}
local owner={IsInSleepMode=function()return sleeping end,GetDevicePS=function()return {IsON=function()return powered end}end,
    QueueEvent=function(_,e)calls.event=e end}
local gc={IsA=function(_,name)return name=='ComputerInkGameController' end,
    GetOwnerEntity=function()return owner end,GetMainLayoutController=function()return layout end,
    IsInteractivityBlocked=function()return blocked end,GoToMenu=function(_,id)calls.menu=id end,
    OpenDocument=function(_,document)calls.document=document end}
local context=C.context(gc,widget)
local wake=C.sleepTarget(context)
assert(wake and C.usable(wake));assert(C.activate(wake))
assert(calls.event.wakeUp and calls.event.menuType==EComputerMenuType.MAIN)
blocked=true;calls.event=nil;assert(not C.activate(wake) and calls.event==nil)
blocked=false;powered=false;assert(not C.activate(wake));powered=true
local buttons={}
local menu={targetWidget=widget,IsA=function(_,n)return n=='ComputerMenuButtonController' end,GetMenuID=function()return 'mails' end}
assert(C.collect(context,widget,menu,'mail',buttons))
assert(not C.activate(buttons[1])) -- hidden menus behind the screen saver cannot run
sleeping=false;assert(C.sleepTarget(context)==nil and not C.activate(wake))
assert(C.activate(buttons[1]) and calls.menu=='mails')
local document={targetWidget=widget,IsA=function(_,n)return n=='ComputerDocumentThumbnailWidgetController' end}
C.collect(context,widget,document,'document',buttons)
assert(C.activate(buttons[2]) and calls.document==document) -- original controller owns reads/quest callbacks
assert(C.activate({computer=context,widget=widget,kind='computer_window_close'}) and calls.close)
local link={GetLinkAddress=function()return 'NETdir://test' end}
local page={CallCustomCallback=function(_,name)calls.callback=name end}
local browser={LoadWebPage=function(_,address)calls.home=address end}
assert(C.activate({computer=context,widget=widget,kind='computer_link',control=link,webPage=page,browser=browser}))
assert(page.lastClickedLinkAddress=='NETdir://test' and calls.callback=='OnLinkPressed' and calls.home==nil)
assert(C.activate({computer=context,widget=widget,kind='computer_link',control=link,browser=browser}))
assert(calls.home=='NETdir://test')
print('PASS: computer wake ownership, blocked/asleep/off guards, native menu/document callbacks and browser links')
