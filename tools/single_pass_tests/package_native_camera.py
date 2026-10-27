"""Package MAIN-prefix camera variants without changing other resource bindings."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from batch_shaders import run,PASSES,classify,save_json
from patch_camera import patch
from upgrade_stage import upgrade
from link_stage_signatures import link_pixel_inputs

AUDITED_VS='17f7e646a02eeaa6ea496dd75a21a494588b29dd926a545fb52bee45dec68117'
TYPES=dict(srv=0,uav=1,cbuffer=2,sampler=3)


def resource_contract(info):
    return sorted((TYPES[r['kind']],r['space'],r['register'],r['count'],r.get('bytes',0)) for r in info['resources'])


def validate_stage(info,stage,uses):
    if info['stage']!=stage or stage not in ('vs','ps'):raise ValueError('Wrong native camera shader stage')
    visibility=1 if stage=='vs' else 5
    expected=sorted((u['type'],u['space'],u['register'],u['count'],u['bytes']) for u in uses if u['visibility']==visibility)
    if resource_contract(info)!=expected:raise ValueError('Native shader resources differ from family contract')
    if info['cameraBindings']:
        c=info['camera']
        if info['cameraBindings']!=1 or (c['register'],c['space'],c['bytes'])!=(1,0,848):
            raise ValueError('Unsupported native camera layout')
    return bool(info['cameraBindings'])


def save_binary(out,data):
    if not 32<=len(data)<=1024*1024:raise ValueError('Unsupported shader binary size')
    digest=hashlib.sha256(data).hexdigest();name=digest+'.dxil'
    (out/name).write_bytes(data)
    return dict(file=name,sha256=digest,bytes=len(data))


def convert_shader(native,stage,out,dxc,assembler):
    sha=native.stem;data=native.read_bytes()
    if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('Native shader hash mismatch')
    ir=out/(sha+'.ll');run([dxc/'dxc.exe','-dumpbin','-Fc',ir,native])
    source=ir.read_text();info=classify(source)
    if info['stage']!=stage:raise ValueError('Wrong shader stage')
    camera=bool(info['cameraBindings'])
    if camera:
        converted,layout=patch(source,main_first=True)
        if (layout['original_size'],layout['eye_stride'],layout['camera_register'],layout['space'])!=(848,1024,1,0):
            raise ValueError('Unsupported native camera layout')
    else:converted=upgrade(source);layout=None
    ir.write_text(converted);binary=out/(sha+'.tmp.dxil')
    validation=run([assembler,dxc,ir,binary,*PASSES])
    entry=save_binary(out,binary.read_bytes());binary.unlink();entry['cameraPatched']=camera
    return dict(entry=entry,original=info,nativeBytes=len(data),irFile=ir.name,layout=layout,validation=validation)


def link_native_stages(vertex,pixel,out,dxc,assembler):
    source,report=link_pixel_inputs((out/vertex['irFile']).read_text(),(out/pixel['irFile']).read_text())
    if not report['changed']:return dict(entry=pixel['entry'],report=report)
    name=hashlib.sha256(source.encode()).hexdigest()
    ir=out/(name+'.linked.ll');ir.write_text(source);binary=out/(name+'.linked.dxil')
    validation=run([assembler,dxc,ir,binary,*PASSES])
    entry=save_binary(out,binary.read_bytes());binary.unlink();entry['cameraPatched']=pixel['entry']['cameraPatched']
    return dict(entry=entry,report=report,validation=validation)


def package(manifest_path,native,out,dxc,assembler,all_families=False):
    if out.exists():raise ValueError('Use a fresh package directory')
    manifest=json.loads(manifest_path.read_text())
    if manifest.get('schema')!=1 or manifest.get('purpose')!='development-linkage-only':raise ValueError('Unsupported family manifest')
    out.mkdir(parents=True);cache={};rows=[]
    for family in manifest['families']:
        for key in ('nativeCameraVs','nativeCameraPs','nativeCamera'):family.pop(key,None)
        for stage in ('vs','ps'):
            if stage not in family:continue
            entry=family[stage]
            if not re.fullmatch('[0-9a-f]{64}',entry['sha256']) or entry['file']!=entry['sha256']+'.dxil':raise ValueError('Invalid existing shader filename')
            data=(manifest_path.parent/entry['file']).read_bytes()
            if hashlib.sha256(data).hexdigest()!=entry['sha256'] or len(data)!=entry['bytes']:raise ValueError('Existing shader hash/size mismatch')
            save_binary(out,data)
        selected=all_families or (family['nativeVs']==AUDITED_VS and not family['nativePs'])
        if not selected:continue
        converted={};mask=0;stages={}
        for stage,bit in (('vs',1),('ps',2)):
            digest=family['native'+stage.title()]
            if not digest:continue
            if not re.fullmatch('[0-9a-f]{64}',digest):raise ValueError('Invalid native shader identity')
            key=(digest,stage)
            if key not in cache:cache[key]=convert_shader(native/(digest+'.bin'),stage,out,dxc,assembler)
            shader=cache[key]
            if shader['nativeBytes']!=family['native'+stage.title()+'Bytes']:raise ValueError('Native shader size differs from family contract')
            camera=validate_stage(shader['original'],stage,family['uses'])
            if shader['entry']['cameraPatched']!=camera:raise ValueError('Camera conversion contract mismatch')
            if camera:mask|=bit
            converted[stage]=shader['entry'];stages[stage]=shader
        link=None
        if mask:
            if 'ps' in stages:
                link=link_native_stages(stages['vs'],stages['ps'],out,dxc,assembler)
                converted['ps']=link['entry']
            family['nativeCamera']=dict(schema=1,mainFirst=True,eyeStride=1024,bindingSize=2048,stages=mask,**converted)
        rows.append(dict(vs=family['nativeVs'],ps=family['nativePs'],stages=mask,link=link,status='validated' if mask else 'no-camera'))
    if not any(r['vs']==AUDITED_VS and not r['ps'] and r['stages']==1 for r in rows):
        raise ValueError('Audited depth family is missing')
    save_json(out/'families.json',manifest)
    report=dict(families=rows,shaders={digest:record for (digest,_),record in cache.items()},
                stageMasks=dict(Counter(r['stages'] for r in rows)),
                limitation='Conversion and linkage candidates only. Other resources, targets and visibility still need per-pass validation.')
    save_json(out/'native-camera-validation.json',report)
    return dict(families=len(rows),cameraFamilies=sum(bool(r['stages']) for r in rows),shaders=len(cache),stageMasks=report['stageMasks'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--dxc',type=Path,required=True)
    parser.add_argument('--assembler',type=Path,required=True)
    parser.add_argument('--all-families',action='store_true')
    args=parser.parse_args()
    print(json.dumps(package(args.manifest,args.native,args.out,args.dxc,args.assembler,args.all_families),indent=2))


if __name__=='__main__':main()
