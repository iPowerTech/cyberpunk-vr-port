"""Single-pass ceiling: live GPU cost of one eye's geometry and of its non-pixel part.

The control CyberpunkVR_GeometryCostProbe (include/Render/GeometryCostProbe.hpp) changes only
draws recorded inside the RenderElements node of the selected eye:
  1 = empty scissor (IA/VS/setup still run; no raster/PS/ROP), 2 = draw not forwarded.
A view-instanced pass can at most remove the empty-scissor cost of one eye, minus its own
per-view position work. Pixel work is unchanged by instancing. Results are interval samples
of the same running game, not an A/B across launches.
"""
import json
import statistics
import struct
import time

RENDER_ELEMENTS = 0x23A938
SIDE0, SIDE1 = 0x10, 0x20
EMPTY_SCISSOR, SKIP = 1, 2
CONFIGS = (
    ('baseline', 0),
    ('main_vertex', EMPTY_SCISSOR | SIDE0),
    ('vrcam_vertex', EMPTY_SCISSOR | SIDE1),
    ('main_skip', SKIP | SIDE0),
    ('vrcam_skip', SKIP | SIDE1),
    ('both_vertex', EMPTY_SCISSOR | SIDE0 | SIDE1),
    ('baseline_end', 0),
)
NAMES = ('CyberpunkVR_GeometryCostProbe', 'CyberpunkVR_GeometryCostNode', 'CyberpunkVR_GeometryCostHits',
         'CyberpunkVR_SceneGpuRequest', 'CyberpunkVR_SceneGpuReport', 'CyberpunkVR_SceneGpuReportSeq',
         'CyberpunkVR_SceneGpuGroupMode', 'CyberpunkVR_SceneGpuNodeFilter',
         'CyberpunkVR_FramegenReport', 'CyberpunkVR_FramegenReportSeq', 'CyberpunkVR_RuntimeDiagnostics')


def summarize_profile(value):
    """Per-side RenderElements and whole-profile sums from one grouped scene profile."""
    if value.get('error') or value.get('active') or not value.get('completed'):
        raise ValueError(f'Scene profile incomplete: {value}')
    sides = {0: dict(renderElementsMs=0.0, renderElementsRanges=0, renderElementsCommands=0, totalMs=0.0),
             1: dict(renderElementsMs=0.0, renderElementsRanges=0, renderElementsCommands=0, totalMs=0.0)}
    for row in value.get('groups', []):
        side = row.get('side')
        if side not in sides:
            continue
        sides[side]['totalMs'] += row['sum_ms']
        if row.get('node') == RENDER_ELEMENTS:
            sides[side]['renderElementsMs'] += row['sum_ms']
            sides[side]['renderElementsRanges'] += row['ranges']
            sides[side]['renderElementsCommands'] += row['commands']
    return dict(completed=value['completed'], discarded=value.get('discarded', 0), sides=sides)


def per_frame(summary, frames):
    """Normalize sampled sums by the number of frames the 256-list sample spanned."""
    if frames <= 0:
        raise ValueError('Profile frame count must be positive')
    return {side: {key: (val / frames if key.endswith('Ms') else val) for key, val in row.items()}
            for side, row in summary['sides'].items()}


def frame_rate(rows):
    """FPS from the native real-frame counter, median GPU frame time from the same window."""
    usable = [row for row in rows if isinstance(row.get('report', {}).get('real'), int)]
    if len(usable) < 2:
        raise ValueError('Too few frame-rate samples')
    first, last = usable[0], usable[-1]
    seconds = last['time'] - first['time']
    frames = last['report']['real'] - first['report']['real']
    if seconds <= 0 or frames <= 0:
        raise ValueError('Frame counter did not advance')
    gpu = [row['report']['gpu_ms'] for row in usable if row['report'].get('gpu_ms', 0) > 0]
    return dict(fps=frames / seconds, frames=frames, seconds=seconds,
                medianGpuMs=statistics.median(gpu) if gpu else None,
                medianGpuUsage=statistics.median(row['report'].get('gpu_usage', 0) for row in usable),
                medianCpuUsage=statistics.median(row['report'].get('cpu_usage', 0) for row in usable),
                medianFrameIntervalMs=statistics.median(row['report'].get('cpu_ms', 0) for row in usable))


def ceiling(results):
    """Derive the single-pass GPU ceiling from measured intervals of one launch."""
    by = {row['label']: row for row in results}
    base = [by[name]['frameRate']['fps'] for name in ('baseline', 'baseline_end') if name in by]
    if not base:
        raise ValueError('No baseline interval')
    baseline_fps = statistics.mean(base)
    out = dict(baselineFps=baseline_fps, baselineDriftFps=(max(base) - min(base)) if len(base) > 1 else 0.0)
    for side, name in ((0, 'main'), (1, 'vrcam')):
        base_ms = statistics.mean(by[label]['perFrame'][side]['renderElementsMs']
                                  for label in ('baseline', 'baseline_end') if label in by)
        entry = dict(nativeRenderElementsMs=base_ms)
        if 'baseline' in by and f'{name}_vertex' in by:
            entry['vertexRemainingFraction'] = relative_fraction(by[f'{name}_vertex'], by['baseline'], side)
        if 'baseline' in by and f'{name}_skip' in by:
            entry['skipRemainingFraction'] = relative_fraction(by[f'{name}_skip'], by['baseline'], side)
        if f'{name}_vertex' in by:
            vertex = by[f'{name}_vertex']
            entry['vertexOnlyRenderElementsMs'] = vertex['perFrame'][side]['renderElementsMs']
            entry['pixelShareOfRenderElements'] = (1 - entry['vertexOnlyRenderElementsMs'] / base_ms) if base_ms > 0 else None
            entry['vertexOnlyFps'] = vertex['frameRate']['fps']
        if f'{name}_skip' in by:
            skip = by[f'{name}_skip']
            entry['skipRenderElementsMs'] = skip['perFrame'][side]['renderElementsMs']
            entry['skipFps'] = skip['frameRate']['fps']
        out[name] = entry
    return out


def validate(results):
    """Fail closed: the probe must have engaged in every non-baseline interval and only there."""
    labels = [row['label'] for row in results]
    if labels != [name for name, _ in CONFIGS]:
        raise ValueError(f'Unexpected interval order: {labels}')
    for row in results:
        engaged = row['hits'] > 0
        if (row['control'] != 0) != engaged:
            raise ValueError(f'Probe engagement mismatch in {row["label"]}: hits={row["hits"]}')
        if row['controlAfter'] != row['control']:
            raise ValueError(f'Control changed during {row["label"]}')
    return True


class LiveProbe:
    """Thin live accessor; all writes are restored by restore()."""

    def __init__(self, reader, symbols, pid, check):
        import ctypes as c
        self.c, self.reader, self.symbols, self.check = c, reader, symbols, check
        self.k = reader.k
        self.k.WriteProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        self.k.WriteProcessMemory.restype = c.c_int
        self.handle = self.k.OpenProcess(0x101028, False, pid)
        if not self.handle:
            raise c.WinError(c.get_last_error())
        self.original = {name: self.read32(name) for name in ('CyberpunkVR_RuntimeDiagnostics', 'CyberpunkVR_SceneGpuGroupMode',
                                                              'CyberpunkVR_SceneGpuNodeFilter')}

    def address(self, name):
        return int(self.symbols[name], 16)

    def read32(self, name):
        return struct.unpack('<I', self.reader.read(self.address(name), 4))[0]

    def read64(self, name):
        return struct.unpack('<Q', self.reader.read(self.address(name), 8))[0]

    def write32(self, name, value):
        data, done = self.c.c_uint32(value), self.c.c_size_t()
        if not self.k.WriteProcessMemory(self.handle, self.address(name), self.c.byref(data), 4, self.c.byref(done)) or done.value != 4:
            raise self.c.WinError(self.c.get_last_error())

    def seqlock(self, body, seq, size):
        for _ in range(200):
            before = self.read32(seq)
            if before & 1:
                continue
            raw = self.reader.read(self.address(body), size).split(b'\0', 1)[0]
            if self.read32(seq) == before:
                return json.loads(raw) if raw else {}
        raise RuntimeError(f'Busy report {body}')

    def profile(self, timeout=10):
        if self.read32('CyberpunkVR_SceneGpuRequest'):
            raise RuntimeError('Scene profile already requested')
        if self.seqlock('CyberpunkVR_SceneGpuReport', 'CyberpunkVR_SceneGpuReportSeq', 262144).get('active'):
            raise RuntimeError('Scene profile already active')
        seq = self.read32('CyberpunkVR_SceneGpuReportSeq')
        self.write32('CyberpunkVR_SceneGpuRequest', 256)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            time.sleep(.2)
            self.check()
            value = self.seqlock('CyberpunkVR_SceneGpuReport', 'CyberpunkVR_SceneGpuReportSeq', 262144)
            if self.read32('CyberpunkVR_SceneGpuReportSeq') != seq and not value.get('active') and value.get('completed'):
                return value
        raise RuntimeError('Scene profile did not finish')

    def frames(self, seconds):
        rows = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.check()
            rows.append(dict(time=time.perf_counter(),
                             report=self.seqlock('CyberpunkVR_FramegenReport', 'CyberpunkVR_FramegenReportSeq', 2048)))
            time.sleep(.25)
        return rows

    def restore(self):
        self.write32('CyberpunkVR_GeometryCostProbe', 0)
        self.write32('CyberpunkVR_GeometryCostNode', 0)
        self.write32('CyberpunkVR_SceneGpuRequest', 0)
        for name, value in self.original.items():
            self.write32(name, value)
        result = {name: self.read32(name) for name in ('CyberpunkVR_GeometryCostProbe', 'CyberpunkVR_GeometryCostNode')}
        self.k.CloseHandle(self.handle)
        self.handle = None
        return result


# Nodes recorded exactly once per eye per frame (src/Stereo/NodeNames.inc). With grouped profiling
# (pso ignored) each contributes one range per frame, so their median range count is the number of
# frames the 256-list sample covered for that eye. Partial boundary frames make this approximate;
# fractions below therefore use the unaffected eye as the in-sample reference instead.
FRAME_NODES = (0x775ACC, 0x772BAC, 0x154610)  # SetRenderTargetsMain, ComputeShadingRateImage, RenderLightsIntegrate


def profile_frames(value):
    frames = {}
    for side in (0, 1):
        counts = [sum(row['ranges'] for row in value.get('groups', []) if row.get('side') == side and row.get('node') == node)
                  for node in FRAME_NODES]
        counts = [count for count in counts if count > 0]
        if not counts:
            raise ValueError(f'No once-per-frame node sampled for side {side}')
        frames[side] = statistics.median(counts)
    return frames


def relative_fraction(mode, baseline, side):
    """Remaining fraction of one eye's RenderElements time, normalized by the other eye in the same sample."""
    other = 1 - side
    try:
        now = mode['profile']['sides'][side]['renderElementsMs'] / mode['profile']['sides'][other]['renderElementsMs']
        before = baseline['profile']['sides'][side]['renderElementsMs'] / baseline['profile']['sides'][other]['renderElementsMs']
    except ZeroDivisionError:
        return None
    return now / before if before > 0 else None


def run(probe, ini_path, *, settle=2.5, seconds=6.0, on_interval=None):
    """Measure every CONFIGS interval in this process; always restores the ini and controls."""
    original_ini = ini_path.read_bytes()
    lines = original_ini.splitlines()
    if b'xr_framegen=0' not in lines:
        raise RuntimeError('Frame generation must stay off for a base-FPS ceiling measurement')
    overlay = next((line for line in lines if line.startswith(b'xr_framegen_overlay=')), None)
    if overlay is None:
        raise RuntimeError('vrport.ini lacks xr_framegen_overlay')
    modified = original_ini.replace(overlay, b'xr_framegen_overlay=1', 1)
    results = []
    try:
        probe.write32('CyberpunkVR_RuntimeDiagnostics', 1)
        probe.write32('CyberpunkVR_SceneGpuGroupMode', 1)
        probe.write32('CyberpunkVR_SceneGpuNodeFilter', 0)
        probe.write32('CyberpunkVR_GeometryCostNode', 0)
        ini_path.write_bytes(modified)
        time.sleep(3)
        for label, control in CONFIGS:
            probe.write32('CyberpunkVR_GeometryCostProbe', control)
            time.sleep(settle)
            hits_before = probe.read64('CyberpunkVR_GeometryCostHits')
            started = time.perf_counter()
            rows = probe.frames(seconds)
            profile = probe.profile()
            elapsed = time.perf_counter() - started
            hits = probe.read64('CyberpunkVR_GeometryCostHits') - hits_before
            rate = frame_rate(rows)
            summary = summarize_profile(profile)
            frames = profile_frames(profile)
            row = dict(label=label, control=control, controlAfter=probe.read32('CyberpunkVR_GeometryCostProbe'),
                       hits=hits, frameRate=rate, profile=summary, profileFrames=frames,
                       perFrame={side: per_frame(summary, frames[side])[side] for side in (0, 1)},
                       elapsedSeconds=elapsed, rawGroups=profile.get('groups', []))
            results.append(row)
            if on_interval:
                on_interval(row)
    finally:
        current = ini_path.read_bytes()
        ini_path.write_bytes(original_ini if current == modified else current.replace(b'xr_framegen_overlay=1', overlay, 1))
        cleanup = probe.restore()
    return results, cleanup
