"""Ordinary-melee regressions: wrist adjustments, short strokes and reversals."""
import math
from pathlib import Path
from lupa import LuaRuntime

ROOT=Path(__file__).resolve().parents[2]

def replay(hz,case,source=None):
    lua=LuaRuntime(unpack_returned_tuples=True)
    lua.globals().package.path=str(ROOT/'mods/cet/CyberpunkVRPort_Weapon/?.lua').replace('\\','/')+';'+lua.globals().package.path
    lua.execute('''
      stamp=0;seq=0;x=0;theta=0;starts=0;sounds=0;active=0;firstSound=-1;lastStart=-1
      function v(x,y,z,w) return {x=x,y=y,z=z,w=w or 1} end
      Vector4={new=v}
      Game={GetVRSharedSlot=function() return 0 end,
        GetSystemRequestsHandler=function() return {IsGamePaused=function() return false end} end,
        VRMeleeInputFrame=function() return {v(stamp,seq,0),v(0,0,1),v(x,0,1),v(0,0,0,1),v(0,0,math.sin(theta/2),math.cos(theta/2))} end}
      player={VRCyberwareBeginSwing=function() starts=starts+1;lastStart=stamp;return true end,
        VRCyberwareUpdateSwing=function() end,
        VRMeleeSweepHit=function(_,w,p,t,oldP,oldT,r,passive) if not passive then active=active+1 end end,
        VRMeleeBladeHit=function(_,w,p,f,r,strong,passive) if not passive then active=active+1 end end,
        VRMeleeWhoosh=function() sounds=sounds+1;if firstSound<0 then firstSound=stamp end end}
      weapon={VRMeleeReach=function() return 1.25 end};origin=v(0,0,0)
    ''')
    m=lua.execute(source or (ROOT/'mods/cet/CyberpunkVRPort_Weapon/cyberware.lua').read_text())
    g=lua.globals();elapsed=0
    def tick(dx=0,theta=None):
        nonlocal elapsed
        elapsed+=1/hz;g.stamp=round(elapsed*1000);g.seq+=1;g.x+=dx
        if theta is not None:g.theta=theta
        m.ordinary(1/hz,g.player,g.weapon,g.v(g.x,0,1),g.v(math.sin(g.theta),math.cos(g.theta),0,0),g.origin)
    for _ in range(hz):tick()
    began=g.stamp
    if case=='wrist':
        steps=max(2,round(.05*hz))
        for i in range(steps):tick(theta=(i+1)/steps*.7)
    elif case=='jitter':
        for i in range(hz):tick(.04 if i%2==0 else -.04)
    elif case=='short':
        for _ in range(max(1,round(.033*hz))):tick(6/hz)
    elif case=='below_sound':
        for _ in range(round(.18*hz)):tick(3.2/hz)
    elif case=='reversal':
        for _ in range(round(.15*hz)):tick(4/hz)
        for _ in range(round(.15*hz)):tick(-4/hz)
    else:raise ValueError(case)
    for _ in range(hz):tick()
    return dict(starts=g.starts,sounds=g.sounds,active=g.active,latency=g.firstSound-began)

if __name__=='__main__':
    for hz in (20,30,60,90,120):
        for case in ('wrist','jitter','short','below_sound','reversal'):
            r=replay(hz,case)
            expected={'wrist':(0,0),'jitter':(0,0),'short':(1,1),'below_sound':(1,0),'reversal':(2,2)}[case]
            assert (r['starts'],r['sounds'])==expected,(hz,case,r)
            if case=='short':assert 0<r['latency']<=max(40,1000/hz),(hz,case,r)
            print(hz,case,r)
