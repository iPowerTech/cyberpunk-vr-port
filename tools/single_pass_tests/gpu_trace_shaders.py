"""Per-shader GPU cost from an Nsight Graphics 2026.3 GPU Trace (real-time shader profiler on).

CPU-only: reads the .ngfx-gputrace, opens no UI, replay or game. Needs the Nsight MCP venv
(lz4 + protobuf schemas extracted from WarpVizPlugin.dll):
  ..\\nsight-graphics-mcp\\.venv\\Scripts\\python.exe gpu_trace_shaders.py TRACE --out shaders.json

What it decodes (docs/shader-profile-20261008.md):
- The first WRPV section is NV.WarpViz.PbTraceData. Hardware PC samples live in
  gpus[0].SamplingData.PcSamplingPerSM[sm].pcSamplesStorage as 16-byte records
  <u64 pc, u32 unused, u32 bits>, bits = flag<<16 | reason<<8 | smsp<<4 | warp.
- Each sample is attributed to the shader code block whose VA range holds its pc. Samples
  outside every block are driver/SDK code (DLSS) and are reported as unattributed.
- Code blocks carry registers per thread and an ELF whose ShaderModuleHash equals the
  PipelineSets apphash, which ties a block to the PSO used by draws/dispatches in the timeline.
- Instruction and ELF blobs are XOR-obfuscated with one fixed mask; it is recovered from known
  plaintext (native DXBC exported from a RenderDoc capture of the same game shaders).
"""
import argparse
import bisect
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path

PLUGIN = Path(r'C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.1\host\windows-desktop-nomad-x64'
              r'\Plugins\WarpVizPlugin\WarpVizPlugin.dll')
NATIVE = Path(__file__).resolve().parents[2] / 'build/gpu-audit-20260930/native-shaders'

# Hardware stall codes on GB203 (RTX 5070 Ti), identified from the SASS instruction each code is
# sampled on (see the doc). 17/18 are selected/not_selected in an unproven order.
REASONS = {2: 'barrier', 6: 'wait', 8: 'long_scoreboard', 9: 'short_scoreboard', 12: 'tex_throttle',
           14: 'lg_throttle', 15: 'mio_throttle', 16: 'math_pipe_throttle', 17: 'selected/not_selected(a)',
           18: 'selected/not_selected(b)', 19: 'drain'}
SHADER_TYPES = {1: 'VS', 7: 'PS', 8: 'CS'}


def reason_name(code):
    return REASONS.get(code, f'code{code}')


def parse_samples(storage):
    """(pc, reason, flag, warp) for every 16-byte PC sample record."""
    if len(storage) % 16:
        raise ValueError('PC sample storage is not a whole number of records')
    for at in range(0, len(storage), 16):
        pc, _, bits = struct.unpack_from('<QII', storage, at)
        yield pc, (bits >> 8) & 0xff, bits >> 16, bits & 0xff


class Blocks:
    """Sorted code-block VA ranges; lookup returns the block index or None."""

    def __init__(self, ranges):
        self.ranges = sorted(ranges)
        self.starts = [r[0] for r in self.ranges]

    def find(self, pc):
        k = bisect.bisect_right(self.starts, pc) - 1
        if k >= 0 and pc < self.ranges[k][0] + self.ranges[k][1]:
            return self.ranges[k][2]
        return None


def sections(data):
    """Decoded bytes of the leading WRPV PbTraceData section."""
    import lz4.block
    if data[:4] != b'WRPV' or struct.unpack_from('<I', data, 4)[0] != 10:
        raise ValueError('not a version-10 WRPV GPU trace')
    magic, count, size = struct.unpack_from('<QQQ', data, 8)
    if magic != 0x11234 or not 0 < count <= 4096:
        raise ValueError('unexpected first section header')
    at, parts = 32, []
    for _ in range(count):
        block_magic, packed, unpacked = struct.unpack_from('<QQQ', data, at)
        if block_magic != 0x14321:
            raise ValueError('unexpected LZ4 block header')
        at += 24
        parts.append(lz4.block.decompress(data[at:at + packed], uncompressed_size=unpacked))
        at += packed
    blob = b''.join(parts)
    if len(blob) != size:
        raise ValueError('section size mismatch')
    return blob


def unpack(blob):
    import lz4.block
    data = lz4.block.decompress(blob.data, uncompressed_size=blob.uncompressedSize) if blob.compression == 2 else blob.data
    if len(data) != blob.uncompressedSize:
        raise ValueError('blob size mismatch')
    return data


def recover_mask(session, blobs, native=NATIVE):
    """Longest XOR mask proven by a size-unique native DXBC; every shorter proof must agree."""
    by_size = defaultdict(list)
    for path in native.glob('*.dxbc'):
        by_size[path.stat().st_size].append(path)
    keys = []
    for module in session.modules:
        if not module.HasField('graphicsAPIModule'):
            continue
        blob = blobs[module.graphicsAPIModule.bytecode.idx]
        if blob.obfuscation == 2 and len(by_size[blob.uncompressedSize]) == 1:
            keys.append(bytes(a ^ b for a, b in zip(unpack(blob), by_size[blob.uncompressedSize][0].read_bytes())))
    keys.sort(key=len, reverse=True)
    if len(keys) < 3:
        raise RuntimeError('fewer than three known-plaintext shaders; mask unproven')
    agree = [k for k in keys[1:] if keys[0][:len(k)] == k]
    if len(agree) < 2:
        raise RuntimeError('known-plaintext masks disagree')
    return keys[0]


def deobfuscate(blob, key, prefix=False):
    """Plain bytes, or None when the mask is shorter than the blob (unless only the proven prefix
    is wanted, e.g. to find the ELF's module hash string near its start)."""
    data = unpack(blob)
    if blob.obfuscation == 1:
        return data
    if blob.obfuscation != 2 or (len(data) > len(key) and not prefix):
        return None
    return bytes(a ^ b for a, b in zip(data, key))


def analyze(path, native=NATIVE):
    from nsight_graphics_mcp.proto_descriptors import build_registry
    registry = build_registry(PLUGIN)
    trace = registry.message_class('NV.WarpViz.PbTraceData').FromString(sections(Path(path).read_bytes()))
    device = trace.devices[trace.mainDeviceIndex]
    profile = device.ShaderProfilerReport
    session = profile.pcCountersInfo.pcSamplingSession
    blobs = {b.id: b for b in profile.blobTable.blobs}
    key = recover_mask(session, blobs, native)
    blocks = Blocks([(cb.shaderInstance.vaRange.address, cb.shaderInstance.vaRange.size, i)
                     for i, cb in enumerate(session.codeBlocks) if cb.HasField('shaderInstance')])
    per_block, unattributed, total = defaultdict(Counter), Counter(), 0
    for sm in trace.gpus[0].SamplingData.PcSamplingPerSM:
        for pc, reason, _, _ in parse_samples(sm.pcSamplesStorage):
            total += 1
            index = blocks.find(pc)
            (unattributed if index is None else per_block[index])[reason] += 1
    hashes = {}
    for i, cb in enumerate(session.codeBlocks):
        shader = cb.shaderInstance
        if shader.HasField('debugInfo') and shader.debugInfo.HasField('ref'):
            elf = deobfuscate(blobs[shader.debugInfo.ref.idx], key, prefix=True)
            found = set(re.findall(rb'ShaderModuleHash>([0-9a-fA-F]+)', elf or b''))
            if len(found) == 1:
                hashes[i] = found.pop().decode().lower().zfill(16)
    frames = device.swapchains[trace.mainSwapChainIndex].PresentedFrames
    uses = calls_by_hash(device, frames)
    rows = []
    for index, reasons in per_block.items():
        shader = session.codeBlocks[index].shaderInstance
        count = sum(reasons.values())
        app = hashes.get(index)
        rows.append(dict(block=index, apphash=app, type=SHADER_TYPES.get(shader.type, str(shader.type)),
                         registers=shader.commonProgramMeta.registersPerThread, sassBytes=shader.vaRange.size,
                         samples=count, share=count / total,
                         reasons={reason_name(r): n / count for r, n in reasons.most_common()},
                         calls=uses.get(app, {})))
    rows.sort(key=lambda r: -r['samples'])
    return dict(trace=str(path), frames=len(frames), frameMs=[(f.End - f.Start) / 1e6 for f in frames],
                totalSamples=total, unattributedShare=sum(unattributed.values()) / total,
                unattributedReasons={reason_name(r): n for r, n in unattributed.most_common()}, blocks=rows)


def calls_by_hash(device, frames):
    """Per shader apphash: work calls per presented frame, queues, and mean bottom-of-pipe range (us)."""
    begin, end = frames[0].Start, frames[-1].End
    pso_hashes = {i: {f'{s.apphash:016x}' for s in p.shaders} for i, p in enumerate(device.PipelineSets)}
    work = {'Dispatch', 'DrawInstanced', 'DrawIndexedInstanced', 'ExecuteIndirect'}
    out = defaultdict(lambda: dict(calls=0, queues=set(), rangeUs=[]))
    for queue in device.CommandQueues:
        for stream in queue.CommandStreams:
            stamps = sorted((ts.NextCallIndex, v.PtimerValue) for ts in stream.Timestamps
                            for v in ts.Values if v.Stage == 100000 and v.PtimerValue)
            pso = -1
            for index, call in enumerate(stream.Calls):
                if call.functionName in ('SetPipelineState', 'SetInitialState'):
                    for argument in call.arguments:
                        if argument.HasField('handleValue') and argument.handleValue.HasField('objectLink'):
                            pso = argument.handleValue.objectLink.Accessor
                if call.functionName not in work or pso not in pso_hashes:
                    continue
                before = [t for n, t in stamps if n <= index]
                after = [t for n, t in stamps if n > index]
                if not before or not after or not begin <= before[-1] < end:
                    continue
                for app in pso_hashes[pso]:
                    row = out[app]
                    row['calls'] += 1
                    row['queues'].add(queue.QueueId)
                    row['rangeUs'].append((after[0] - before[-1]) / 1e3)
    return {app: dict(callsPerFrame=v['calls'] / len(frames), queues=sorted(v['queues']),
                      meanRangeUs=sum(v['rangeUs']) / len(v['rangeUs'])) for app, v in out.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--native', type=Path, default=NATIVE)
    parser.add_argument('--top', type=int, default=25)
    args = parser.parse_args()
    result = analyze(args.trace, args.native)
    args.out.write_text(json.dumps(result, indent=1) + '\n', encoding='utf-8')
    print('samples %d, unattributed (driver/SDK) %.1f%%' % (result['totalSamples'], 100 * result['unattributedShare']))
    for r in result['blocks'][:args.top]:
        top = ', '.join('%s %.0f%%' % (k, 100 * v) for k, v in list(r['reasons'].items())[:3])
        calls = r['calls']
        print('%5.2f%% %s regs %3d %s calls/frame %5.1f range %6.1f us | %s' % (
            100 * r['share'], r['type'], r['registers'], r['apphash'], calls.get('callsPerFrame', 0),
            calls.get('meanRangeUs', 0), top))


if __name__ == '__main__':
    main()
