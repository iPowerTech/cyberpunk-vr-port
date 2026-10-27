"""Run the production revolver-trigger block with Float32 controller samples."""
import struct
from pathlib import Path
from lupa import LuaRuntime

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'mods/cet/CyberpunkVRPort_Reload/reload/reload.lua').read_text()
start=source.index("        local ta = (type(GetVRSharedSlot)")
end=source.index("        -- THE SHOT LETS IT GO",start)
block=source[start:end]

for cocked in (False,True):
    lua=LuaRuntime(unpack_returned_tuples=True)
    lua.execute('''
        hamS={p=0,want=0};hatchS={p=0};mc2={saBreak=.85,daBreak=.85,release=.20,dryDrop=.035}
        dt=.016;mode=0;drops=0;raw=0
        function GetVRSharedSlot() return raw end
        function setTrg(v) mode=v end
        function playSnd() drops=drops+1 end
        function clearGrip() end
    ''')
    g=lua.globals()
    if cocked:g.hamS.want=1;g.hamS.p=1
    tick=lua.eval('function() '+block+' end')
    def sample(x,count=1):
        g.raw=struct.unpack('<f',struct.pack('<f',x))[0]
        for _ in range(count):tick()
    sample(.84);assert g.mode==1 and g.drops==0
    sample(.85,5);assert g.mode==2 and g.drops==1
    sample(.21,5);sample(.85,5);assert g.drops==1
    sample(.20,5);assert not g.hamS.fired
    sample(.85,5);assert g.mode==2 and g.drops==2
    g.hatchS.p=1;sample(.85,5);assert g.mode==1
print('Revolver SA/DA Float32 break/reset and crane-open block passed')
