"""Which shader_final.cache shaders (and port twins) a RenderDoc capture's frame actually ran -- read from the
capture FILE (no replay): pipelines bound by SetPipelineState in the frame, their bytecode blobs matched by the
DXIL container hash (bytes 4..20) against build/patchday/shaders-2.5/all_<kind> (shader_final.cache),
static_<kind> (staticshader_final.cache) and src/Shaders/<group>.

  py -3.13 -B frame_shaders.py <capture.rdc> <out.json>"""
import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PYMODULES = r'C:\Users\dariulone\Desktop\renderdoc-src\x64\Release\pymodules'


def renderdoc():
    sys.path.insert(0, PYMODULES)
    os.add_dll_directory(PYMODULES)
    import renderdoc as rd
    return rd


def known_blobs():
    names = {}
    for kind, folder in (('ps', 'all_ps'), ('cs', 'all_cs'), ('vs', 'all_vs'),
                         ('sps', 'static_ps'), ('scs', 'static_cs'), ('svs', 'static_vs')):
        for f in (REPO / 'build/patchday/shaders-2.5' / folder).glob('*.dxil'):
            with open(f, 'rb') as fh:
                names.setdefault(fh.read(20)[4:20], f'{kind}/{f.stem}')
    for group in (REPO / 'src/Shaders').iterdir():
        if group.is_dir():
            for f in group.glob('*.dxil'):
                with open(f, 'rb') as fh:
                    names.setdefault(fh.read(20)[4:20], f'twin:{group.name}/{f.stem}')
    return names


def main():
    rd = renderdoc()
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(os.path.abspath(sys.argv[1]), '', None)
    if st.code != rd.ResultCode.Succeeded:
        sys.exit('open failed: %s' % st.code)
    sd = cap.GetStructuredData()
    names = known_blobs()

    def blobs(chunk):
        out = []

        def walk(o, depth, stage):
            for i in range(o.NumChildren()):
                ch = o.GetChild(i)
                if ch.type.basetype == rd.SDBasic.Buffer:
                    data = bytes(sd.buffers[ch.AsInt()][:20])
                    if data[:4] in (b'DXBC', b'DXIL'):
                        out.append((stage, names.get(data[4:20], 'unknown:' + data[4:20].hex())))
                if ch.NumChildren() and depth < 4:
                    walk(ch, depth + 1, ch.name if ch.name in ('VS', 'PS', 'CS', 'DS', 'HS', 'GS', 'AS', 'MS') else stage)
        walk(chunk, 0, '?')
        return out

    def resource(chunk, field):
        for i in range(chunk.NumChildren()):
            ch = chunk.GetChild(i)
            if ch.name == field:
                return str(ch.AsResourceId())
        return None

    created, bound = {}, {}
    marker = ''
    for idx in range(len(sd.chunks)):
        c = sd.chunks[idx]
        if 'Pipeline' in c.name and 'Create' in c.name:
            pso = resource(c, 'pPipelineState')
            if pso:
                created[pso] = blobs(c)
        elif c.name.endswith('SetMarker') or c.name.endswith('BeginEvent'):
            for i in range(c.NumChildren()):
                ch = c.GetChild(i)
                if ch.name in ('MarkerText', 'Metadata') and ch.type.basetype == rd.SDBasic.String:
                    marker = ch.AsString()
        elif c.name.endswith('SetPipelineState'):
            pso = resource(c, 'pPipelineState')
            if pso:
                bound.setdefault(pso, marker)
    result = {}
    for pso, mark in bound.items():
        for field, name in created.get(pso, []):
            entry = result.setdefault(name, {'fields': set(), 'markers': set()})
            entry['fields'].add(field)
            entry['markers'].add(mark)
    out = {k: {'fields': sorted(v['fields']), 'markers': sorted(v['markers'])[:6]} for k, v in result.items()}
    Path(sys.argv[2]).write_text(json.dumps(out, indent=0), encoding='utf-8')
    print('pipelines bound', len(bound), 'created', len(created), 'shaders', len(out),
          'unknown', sum(k.startswith('unknown') for k in out), 'twins', sum(k.startswith('twin') for k in out))


if __name__ == '__main__':
    main()
