"""Merge reduced tracers verbatim and recalibrate the supplied muzzle mod.

Inputs are extracted resources plus WolvenKit JSON exports. Only the particle
size/emission constants changed by the supplied mod are recalibrated to 30% of
the current game's originals. The unrelated launcher explosion is excluded.
After JSON->CR2W, assemble() preserves every pre-existing archive entry bytewise.
"""
import argparse,copy,hashlib,json,shutil
from pathlib import Path


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def prepare(root):
    output=root/'muzzle30-json';output.mkdir(exist_ok=True)
    manifest=[]
    for source in sorted((root/'muzzle60').rglob('*.effect')):
        rel=source.relative_to(root/'muzzle60')
        if source.name=='w_apl_explosion.effect': continue
        mod=json.loads((root/'muzzle60-json'/(source.name+'.json')).read_text())
        base=json.loads((root/'base-muzzle-json'/(source.name+'.json')).read_text())
        adjusted=copy.deepcopy(base)
        a=adjusted['Data']['RootChunk'];b=mod['Data']['RootChunk']
        assert len(a['events'])==len(b['events']),str(rel)
        edits=[]
        for i,(native,event) in enumerate(zip(a['events'],b['events'])):
            native=native.get('Data',{});event=event.get('Data',{})
            assert native.get('$type')==event.get('$type'),str(rel)
            if native.get('$type')!='effectTrackItemParticles':continue
            for field in ('size','emissionScale'):
                n=native[field]['evaluator'].get('Data',{})
                m=event[field]['evaluator'].get('Data',{})
                if n==m:continue
                assert n['$type']==m['$type']=='CEvaluatorFloatConst',(rel,field,n,m)
                original=n['value'];assert original>0 and 0<=m['value']<original
                n['value']=original*.30
                edits.append(dict(event=i,field=field,original=original,supplied=m['value'],value=n['value']))
        adjusted['Header']['ArchiveFileName']=str(rel).replace('/','\\')
        (output/(source.name+'.json')).write_text(json.dumps(adjusted,indent=2)+'\n')
        manifest.append(dict(path=rel.as_posix(),sourceSHA256=sha(source),edits=edits))
    assert len(manifest)==153
    (root/'muzzle-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('muzzle files:',len(manifest),'constants:',sum(len(x['edits']) for x in manifest))


def assemble(root):
    merged=root/'cyberpunkvrport';merged.mkdir(exist_ok=True)
    old=list((root/'original').rglob('*'))
    for p in old:
        if p.is_file():
            dest=merged/p.relative_to(root/'original');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
    imported=[]
    for p in (root/'tracers').rglob('*.effect'):
        rel=p.relative_to(root/'tracers');dest=merged/rel
        assert not dest.exists(),f'Existing override would be replaced: {rel}'
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        imported.append(dict(path=rel.as_posix(),kind='tracer',sha256=sha(p)))
    for row in json.loads((root/'muzzle-manifest.json').read_text()):
        rel=Path(row['path']);p=root/'muzzle30'/rel.name;dest=merged/rel
        assert p.is_file() and not dest.exists(),str(rel)
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        imported.append(dict(path=rel.as_posix(),kind='muzzle',sha256=sha(p)))
    for p in old:
        if p.is_file(): assert sha(p)==sha(merged/p.relative_to(root/'original'))
    assert len(imported)==276
    (root/'imports.json').write_text(json.dumps(imported,indent=2)+'\n')
    print('preserved originals:',sum(p.is_file() for p in old),'added:',len(imported))


def verify(root):
    total=0
    for row in json.loads((root/'muzzle-manifest.json').read_text()):
        doc=json.loads((root/'verified-json'/(Path(row['path']).name+'.json')).read_text())
        expected=json.loads((root/'muzzle30-json'/(Path(row['path']).name+'.json')).read_text())
        # The binary writer may round Float values. Compare the whole semantic
        # tree as well as each intended constant, not just output file counts.
        def same(a,b):
            if isinstance(a,dict):return a.keys()==b.keys() and all(same(a[k],b[k]) for k in a)
            if isinstance(a,list):return len(a)==len(b) and all(same(x,y) for x,y in zip(a,b))
            if isinstance(a,(int,float)) and isinstance(b,(int,float)):return abs(a-b)<=1e-6*max(1,abs(a))
            return a==b
        assert same(expected['Data'],doc['Data']),row['path']
        for edit in row['edits']:
            value=doc['Data']['RootChunk']['events'][edit['event']]['Data'][edit['field']]['evaluator']['Data']['value']
            assert abs(value/edit['original']-.3)<1e-6,(row['path'],edit,value)
            total+=1
    print('Binary roundtrip: 153 effects,',total,'constants at 30% of native')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['prepare','assemble','verify']);p.add_argument('root',type=Path)
    args=p.parse_args();globals()[args.mode](args.root.resolve())
