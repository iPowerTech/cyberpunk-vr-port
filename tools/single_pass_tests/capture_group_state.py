"""Fresh D3D12 state/target history at the proposed common geometry group."""
import argparse
import json
import os
from pathlib import Path
import sys

p = argparse.ArgumentParser()
p.add_argument('capture')
p.add_argument('--module', default=r'C:\Users\dariulone\Desktop\renderdoc-src\x64\Release\pymodules')
p.add_argument('--out', required=True)
p.add_argument('--events', nargs='+', type=int, default=[10042, 33519])
a = p.parse_args()
sys.path.insert(0, a.module)
dll_path = os.add_dll_directory(a.module)
import renderdoc as rd

def value(v, depth=0):
    if isinstance(v, (bool, int, float, str)) or v is None:
        return v
    if depth >= 6:
        return str(v)
    if hasattr(v, '__iter__') or (hasattr(v, '__len__') and hasattr(v, '__getitem__')):
        return [value(x, depth + 1) for x in v]
    if isinstance(v, rd.ResourceId):
        return str(v)
    names = [n for n in dir(v) if not n.startswith('_') and n not in ('thisown', 'this')]
    out = {}
    for n in names:
        x = getattr(v, n)
        if not callable(x):
            out[n] = value(x, depth + 1)
    return out or str(v)

cap = rd.OpenCaptureFile()
controller = None
try:
    result = cap.OpenFile(a.capture, '', None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError(str(result))
    result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError(str(result))
    actions = {}
    def walk(nodes):
        for n in nodes:
            actions[n.eventId] = n
            walk(n.children)
    walk(controller.GetRootActions())
    def action(eid):
        n = actions.get(eid)
        if n is None:
            return {'event': eid}
        return {'event': eid, 'name': n.GetName(controller.GetStructuredFile()),
                'flags': str(n.flags), 'indices': n.numIndices, 'instances': n.numInstances}
    rows = []
    for eid in a.events:
        controller.SetFrameEvent(eid, True)
        state = controller.GetD3D12PipelineState()
        pipe = controller.GetPipelineState()
        targets = [x.resource for x in pipe.GetOutputTargets() if int(x.resource)]
        targets.append(pipe.GetDepthTarget().resource)
        row = {'event': eid, 'om': value(state.outputMerger),
               'root': value(state.rootSignature), 'raster': value(state.rasterizer),
               'targetStates': [value(x) for x in state.resourceStates if x.resourceId in targets],
               'history': {}}
        for target in targets:
            history = [x for x in controller.GetUsage(target) if x.eventId <= eid]
            row['history'][str(target)] = [{**action(x.eventId), 'usage': str(x.usage)} for x in history[-24:]]
        rows.append(row)
    Path(a.out).write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(json.dumps({'output': a.out, 'events': a.events}))
finally:
    if controller is not None:
        controller.Shutdown()
    cap.Shutdown()
    dll_path.close()
