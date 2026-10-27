"""Double only the cabin steering-wheel transform in native road-car graphs.

Use WolvenKit exports in vehicle-graphs-json, then convert the generated patched
JSON back to CR2W. Bikes, aircraft, trains and tanks are explicitly excluded.
Verify the converted output against native JSON before merging the archive.
"""
import argparse,copy,hashlib,json
from pathlib import Path

def nodes(root):
    pending=[(root,[])]
    while pending:
        value,path=pending.pop()
        if isinstance(value,dict):
            yield value,path
            pending.extend((v,path+[k]) for k,v in value.items())
        elif isinstance(value,list):pending.extend((v,path+[i]) for i,v in enumerate(value))

def name(node,field):return node.get(field,{}).get('name',{}).get('$value')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def prepare(root):
    out=root/'patched-json';out.mkdir(exist_ok=True)
    rows=[]
    for source in sorted((root/'vehicle-graphs-json').glob('*.json')):
        doc=json.loads(source.read_text())
        absolute=Path(doc['Header']['ArchiveFileName'])
        rel=absolute.relative_to(root/'vehicle-graphs')
        common=rel.as_posix()=='base/vehicles/common/gameplay/default_car_anim_graph.animgraph'
        if not common and rel.parts[2] not in ('sport','standard','utility'):continue
        changes=[]
        for node,path in nodes(doc['Data']):
            if node.get('$type')!='animAnimNode_FloatTrackDirectConnConstraint' or name(node,'transformIndex')!='steering_wheel_turn':continue
            assert name(node,'floatTrackIndex')=='steer_turn_rate',str(rel)
            assert node['channel']=='RotY' and node['mulFactorNode']['node'] is None,str(rel)
            assert node['mulFactor']==1,str(rel)
            node['mulFactor']=2
            changes.append(path+['mulFactor'])
        if not changes:continue
        assert len(changes)==1,str(rel)
        doc['Header']['ArchiveFileName']=str(rel)
        (out/source.name).write_text(json.dumps(doc,separators=(',',':'))+'\n')
        rows.append(dict(path=rel.as_posix(),changes=changes,nativeSHA256=sha(absolute)))
    assert any(row['path']=='base/vehicles/common/gameplay/default_car_anim_graph.animgraph' for row in rows)
    (root/'wheel-graphs-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('Road-car cabin graphs:',len(rows),'; each changes only steering_wheel_turn.mulFactor 1 -> 2')

def verify(root):
    rows=json.loads((root/'wheel-graphs-manifest.json').read_text())
    for row in rows:
        filename=Path(row['path']).name+'.json'
        native=json.loads((root/'vehicle-graphs-json'/filename).read_text())['Data']
        actual=json.loads((root/'verified-json'/filename).read_text())['Data']
        for path in row['changes']:
            target=actual
            for key in path[:-1]:target=target[key]
            assert target[path[-1]]==2,(row['path'],path)
            target[path[-1]]=1
        assert actual==native,'Unintended graph change: '+row['path']
    print('Binary roundtrip verified:',len(rows),'cabin gains doubled; every other graph property unchanged')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['prepare','verify']);parser.add_argument('root',type=Path)
    args=parser.parse_args();globals()[args.mode](args.root.resolve())
