"""Find shaders that rebuild a view ray as  tangent * ndc  with the tangent taken from a constant buffer and no
off-centre term: the pattern that ignores the asymmetric projection's NDC shift (the volumetric fog had it, and
screen-space reflections: b6 row 17.zw * (2*uv - 1)).

An NDC-like value is  a + (-1.0)  or  1.0 - a  (also  fmad(a, 2, -1)), where a comes from the thread id or the
pixel position through casts and multiplies. A hit is a multiply of such a value by a constant-buffer component.
For every hit the shader's other loads of the same register are listed, so a matrix that carries P20/P21 can be
told apart from a lone tangent.

  py -3.13 -B ndc_tangent_scan.py <census dir with census.json and shaders/*.ll> [node names .inc]"""
import json,re,sys
from pathlib import Path

DEF=re.compile(r'^\s*(%\d+) = (.*)$')
CB=re.compile(r'call %dx\.types\.CBufRet\.(?:f32|i32) @dx\.op\.cbufferLoadLegacy\.(?:f32|i32)\(i32 59, %dx\.types\.Handle (%\d+), i32 (\d+)\)')
HANDLE=re.compile(r'call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 (\d), i32 (\d+), i32 (\d+),')
EXTRACT=re.compile(r'extractvalue %dx\.types\.CBufRet\.(?:f32|i32) (%\d+), (\d)')
SOURCES=('@dx.op.threadId','@dx.op.groupId','@dx.op.flattenedThreadIdInGroup','@dx.op.loadInput','@dx.op.threadIdInGroup','@dx.op.bufferLoad','@dx.op.rawBufferLoad')   # tile lists of indirect dispatches count as positions

def scan(text):
    defs={}
    for line in text.splitlines():
        m=DEF.match(line)
        if m:defs[m.group(1)]=m.group(2)
    handles={h:(int(c),int(reg)) for h,(c,_,reg) in ((k,HANDLE.search(v).groups()) for k,v in defs.items() if HANDLE.search(v))}
    loads={}
    for k,v in defs.items():
        m=CB.search(v)
        if m and m.group(1) in handles and handles[m.group(1)][0]==2:loads[k]=(handles[m.group(1)][1],int(m.group(2)))
    comps={}
    for k,v in defs.items():
        m=EXTRACT.search(v)
        if m and m.group(1) in loads:comps[k]=(*loads[m.group(1)],int(m.group(2)))
    pos_memo={}
    def from_position(v,depth=0):
        # derived from thread id / pixel position through casts, adds and multiplies by constants
        if v in pos_memo:return pos_memo[v]
        if depth>8 or v not in defs:return False
        d=defs[v];pos_memo[v]=False
        if any(s in d for s in SOURCES):pos_memo[v]=True;return True
        if re.match(r'(uitofp|sitofp|fptoui|fptosi|bitcast|zext|sext|trunc) ',d) or re.match(r'(f?mul|f?add|fsub|shl|and|or|lshr|ashr) ',d) or 'FMad' in d:
            ops=re.findall(r'%\d+',d.split(';')[0])
            r=any(from_position(o,depth+1) for o in ops if o!=v)
            pos_memo[v]=r;return r
        return False
    ndc=set()
    for k,v in defs.items():
        body=v.split(';')[0]
        m=re.match(r'fadd (?:fast )?float (%\d+), -1\.000000e\+00$',body) or re.match(r'fadd (?:fast )?float -1\.000000e\+00, (%\d+)$',body)
        if m and from_position(m.group(1)):ndc.add(k);continue
        m=re.match(r'fsub (?:fast )?float 1\.000000e\+00, (%\d+)$',body)
        if m and from_position(m.group(1)):ndc.add(k);continue
        m=re.match(r'call float @dx\.op\.tertiary\.f32\(i32 46, float (%\d+), float 2\.000000e\+00, float -1\.000000e\+00\)',body)
        if m and from_position(m.group(1)):ndc.add(k)
    hits=[]
    for k,v in defs.items():
        body=v.split(';')[0]
        m=re.match(r'fmul (?:fast )?float (%\d+), (%\d+)$',body)
        pairs=[]
        if m:pairs=[(m.group(1),m.group(2)),(m.group(2),m.group(1))]
        m2=re.match(r'call float @dx\.op\.tertiary\.f32\(i32 46, float (%\d+), float (%\d+), float',body)
        if m2:pairs+= [(m2.group(1),m2.group(2)),(m2.group(2),m2.group(1))]
        for a,b in pairs:
            if a in ndc and b in comps:hits.append((k,a,comps[b]))
    regs={}
    for (reg,row) in loads.values():regs.setdefault(reg,set()).add(row)
    return hits,regs

def main():
    d=Path(sys.argv[1]);census=json.loads((d/'census.json').read_text())
    names={}
    inc=Path(sys.argv[2]) if len(sys.argv)>2 else None
    if inc and inc.exists():
        for m in re.finditer(r'\{ 0x([0-9A-F]+), "([^"]+)" \}',inc.read_text()):names[int(m.group(1),16)]=m.group(2)
    nodes={}
    for r in census:nodes.setdefault(r['hash'],set()).add(names.get(int(r['node'],16),r['node']))
    for ll in sorted((d/'shaders').glob('*.ll')):
        hits,regs=scan(ll.read_text(errors='replace'))
        if not hits:continue
        uses=sorted({f'b{r}[{row}].{"xyzw"[c]}' for _,_,(r,row,c) in hits})
        print(ll.stem,sorted(nodes.get(ll.stem,[])),'ndc x',uses,'| rows of those registers:',
              {f'b{r}':sorted(regs[r]) for r in sorted({h[2][0] for h in hits})})

if __name__=='__main__':main()
