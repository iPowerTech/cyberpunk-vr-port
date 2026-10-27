"""Compile real SM6.0 VS/PS and run the production camera-only DXIL conversion."""
import argparse
import hashlib
from pathlib import Path

from batch_shaders import run,save_json,PASSES
from package_native_camera import convert_shader,link_native_stages,save_binary
from upgrade_stage import upgrade


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dxc',type=Path,required=True)
    p.add_argument('--assembler',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--four-targets',action='store_true')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);report={}
    for stage in ('vs','ps'):
        source=a.out/(stage+'-original.dxil')
        run([a.dxc/'dxc.exe','-T',stage+'_6_0','-E',stage.upper(),'-Fo',source,*(['-D','FOUR_TARGETS=1'] if a.four_targets else []),a.source])
        data=source.read_bytes();native=a.out/(hashlib.sha256(data).hexdigest()+'.bin');native.write_bytes(data)
        result=convert_shader(native,stage,a.out,a.dxc,a.assembler)
        if not result['entry']['cameraPatched']:raise ValueError('Fixture did not exercise camera conversion')
        (a.out/(stage+'-camera.dxil')).write_bytes((a.out/result['entry']['file']).read_bytes())
        report[stage]=result
    linked=link_native_stages(report['vs'],report['ps'],a.out,a.dxc,a.assembler)
    (a.out/'ps-camera.dxil').write_bytes((a.out/linked['entry']['file']).read_bytes())
    report['link']=linked
    negative_ir=a.out/'ps-main-only.ll'
    run([a.dxc/'dxc.exe','-dumpbin','-Fc',negative_ir,a.out/'ps-original.dxil'])
    negative_ir.write_text(upgrade(negative_ir.read_text()))
    negative_binary=a.out/'ps-main-only.tmp.dxil'
    run([a.assembler,a.dxc,negative_ir,negative_binary,*PASSES])
    negative_entry=save_binary(a.out,negative_binary.read_bytes());negative_entry['cameraPatched']=False
    negative=link_native_stages(report['vs'],dict(irFile=negative_ir.name,entry=negative_entry),a.out,a.dxc,a.assembler)
    (a.out/'ps-main-only.dxil').write_bytes((a.out/negative['entry']['file']).read_bytes());negative_binary.unlink()
    report['negative']=negative
    if a.four_targets:
        source=a.out/'ps3-original.dxil'
        run([a.dxc/'dxc.exe','-T','ps_6_0','-E','PS3','-D','FOUR_TARGETS=1','-Fo',source,a.source])
        data=source.read_bytes();native=a.out/(hashlib.sha256(data).hexdigest()+'.bin');native.write_bytes(data)
        third=convert_shader(native,'ps',a.out,a.dxc,a.assembler)
        third_link=link_native_stages(report['vs'],third,a.out,a.dxc,a.assembler)
        (a.out/'ps3-camera.dxil').write_bytes((a.out/third_link['entry']['file']).read_bytes())
        report['ps3']=dict(shader=third,link=third_link)
    save_json(a.out/'conversion.json',report)


if __name__=='__main__':main()
