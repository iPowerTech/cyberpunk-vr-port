"""Run the real captured VS/geometry through two draws and one view-instanced draw.

Requires the private exported capture fixture; does not launch or modify the game.
"""
import argparse
import itertools
import json
import subprocess
import sys
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe',type=Path)
parser.add_argument('fixture',type=Path)
parser.add_argument('--mesh-pass',action='store_true')
parser.add_argument('--resource-pairs',type=Path)
parser.add_argument('--pixel-pairs',type=Path,nargs=2,metavar=('ORIGINAL_PS','PAIRED_PS'))
parser.add_argument('--unlinked-pixel',type=Path,help='Negative control: same pixel shader before ViewID interface normalization')
parser.add_argument('--test-winding',action='store_true',help='Exercise both orientations with a front-face-dependent pixel fixture')
parser.add_argument('--owned-bank',action='store_true',help='Own CBVs and snapshot SRVs before poisoning their native sources')
parser.add_argument('--alias-inputs',action='store_true',help='Both eye producers reuse the very same buffer and texture')
parser.add_argument('--out',type=Path)
args=parser.parse_args()
if args.resource_pairs:args.mesh_pass=True
if args.pixel_pairs and not args.resource_pairs:parser.error('--pixel-pairs requires --resource-pairs')
if args.unlinked_pixel and not args.pixel_pairs:parser.error('--unlinked-pixel requires --pixel-pairs')
if args.test_winding and not args.pixel_pairs:parser.error('--test-winding requires --pixel-pairs')
if args.owned_bank and not args.pixel_pairs:parser.error('--owned-bank requires --pixel-pairs')
if args.alias_inputs and not args.owned_bank:parser.error('--alias-inputs requires --owned-bank')
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'quickboot_tests'))
from launch_to_save import process_ids
if any(process_ids(name) for name in ('Cyberpunk2077.exe','qrenderdoc.exe','renderdoccmd.exe')):
    raise RuntimeError('Close the game and capture replay before standalone GPU comparisons')
rows=[]
bank_options=(['owned-bank'] if args.owned_bank else [])+(['alias-inputs'] if args.alias_inputs else [])
cases=[(1,0,0,1,-1,3)]
cases += [(scale,shift,roll,instances,-1,3) for scale,shift,roll,instances in
          itertools.product((8,16),(0,.08),(-15,15),(1,3))]
cases += [(16,.08,15,3,eye,3) for eye in (0,1)]
cases += [(16,.08,15,3,-1,mask) for mask in (0,1,2)]
if args.mesh_pass:
    cases=[(scale,shift,roll,5,-1,3) for scale,shift,roll in
           itertools.product((32,48),(0,.08),(-15,15))]
    # The captured object can leave one frustum after an extra clip-space
    # translation. Test explicit clipping at zero translation so the other
    # eye still has nonempty reference coverage.
    cases += [(scale,0,roll,5,clip,3) for scale,roll,clip in itertools.product((32,48),(-15,15),(0,1))]
for scale,shift,roll,instances,clip,mask,flip in [(*case,flip) for case in cases for flip in ((False,True) if args.test_winding else (False,))]:
    command=[str(args.exe),str(args.fixture),str(scale),str(shift),str(roll),str(instances),str(clip),str(mask)]
    if args.resource_pairs:command.extend(['resource-pairs',str(args.resource_pairs)])
    elif args.mesh_pass:command.append('mesh-pass')
    if args.pixel_pairs:command.extend(map(str,args.pixel_pairs))
    if flip:command.append('reverse-winding')
    command.extend(bank_options)
    proc=subprocess.run(command,capture_output=True,text=True,timeout=15)
    if proc.returncode:raise RuntimeError(str(command)+'\n'+proc.stdout+'\n'+proc.stderr)
    row=json.loads(proc.stdout);row.update(roll=roll,clip=clip,reverseWinding=flip)
    assert row['differentPixels']==0
    rows.append(row)
result={'cases':len(rows),'result':'PASS','rows':rows}
result.update(ownedBank=args.owned_bank,aliasedSources=args.alias_inputs)
if args.test_winding:
    if any(a['referenceColorHash']==b['referenceColorHash'] for a,b in zip(rows[::2],rows[1::2])):
        raise RuntimeError('Front-face fixture did not change with reversed winding')
    result['frontFaceBranchVerified']=True
if args.unlinked_pixel:
    command=[str(args.exe),str(args.fixture),'32','0','15','5','-1','3','resource-pairs',str(args.resource_pairs),
             str(args.pixel_pairs[0]),str(args.unlinked_pixel),*bank_options]
    rejected=subprocess.run(command,capture_output=True,text=True,timeout=15)
    if (rejected.returncode!=1 or 'PSO mode 1' not in rejected.stderr or
            'validatingPS stage' not in rejected.stderr or 'view instancing' not in rejected.stderr):
        raise RuntimeError('Unlinked-interface control did not fail during the expected PS validation')
    result['unlinkedInterfaceControl']=dict(exitCode=rejected.returncode,diagnostic=rejected.stderr)
if args.pixel_pairs:
    command=[str(args.exe),str(args.fixture),'32','0','15','5','-1','3','resource-pairs',str(args.resource_pairs),*map(str,args.pixel_pairs),'swap-srvs',*bank_options]
    negative=subprocess.run(command,capture_output=True,text=True,timeout=15)
    diagnostic=json.loads(negative.stdout)
    if (negative.returncode!=1 or diagnostic['differentPixels']==0 or diagnostic['differentDepthPixels']!=0
            or 'view instancing differs' not in negative.stderr):
        raise RuntimeError('Swapped-resource negative control did not fail for the expected reason')
    result['negativeControl']=diagnostic
(args.out or args.fixture/('mesh-pass-results.json' if args.mesh_pass else 'comparison-results.json')).write_text(json.dumps(result,indent=2))
print(json.dumps({'cases':len(rows),'differentPixels':sum(x['differentPixels'] for x in rows),
                  'maxChannelError':max(x['maxChannelError'] for x in rows),'maxCovered':max(max(x['covered']) for x in rows)}))
