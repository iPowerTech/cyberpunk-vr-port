"""Per-thread CPU time of a live game over a window: finds the CPU critical-path thread.

Uses Toolhelp thread enumeration, GetThreadTimes and GetThreadDescription. Read-only; it never
suspends or modifies a thread. A thread near 100% of one core is a frame-rate limiter.
"""
import argparse
import ctypes as c
import ctypes.wintypes as w
import json
import time

k = c.WinDLL('kernel32', use_last_error=True)


class THREADENTRY32(c.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD), ('th32OwnerProcessID', w.DWORD),
                ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]


k.CreateToolhelp32Snapshot.restype = w.HANDLE
k.OpenThread.restype = w.HANDLE
k.GetThreadTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
GetThreadDescription = getattr(k, 'GetThreadDescription', None)
if GetThreadDescription:
    GetThreadDescription.argtypes = [w.HANDLE, c.POINTER(c.c_wchar_p)]
k.LocalFree.argtypes = [c.c_void_p]


def threads(pid):
    snap = k.CreateToolhelp32Snapshot(4, 0)
    entry = THREADENTRY32()
    entry.dwSize = c.sizeof(entry)
    out = []
    ok = k.Thread32First(snap, c.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            out.append(entry.th32ThreadID)
        ok = k.Thread32Next(snap, c.byref(entry))
    k.CloseHandle(snap)
    return out


def sample(tid):
    handle = k.OpenThread(0x0800 | 0x0040, False, tid)  # QUERY_LIMITED_INFORMATION | QUERY_INFORMATION
    if not handle:
        return None
    try:
        times = [w.FILETIME() for _ in range(4)]
        if not k.GetThreadTimes(handle, *[c.byref(t) for t in times]):
            return None
        busy = sum((t.dwHighDateTime << 32 | t.dwLowDateTime) for t in times[2:]) / 1e7
        name = ''
        if GetThreadDescription:
            text = c.c_wchar_p()
            if GetThreadDescription(handle, c.byref(text)) >= 0 and text.value:
                name = text.value
                k.LocalFree(text)
        return busy, name
    finally:
        k.CloseHandle(handle)


def measure(pid, seconds):
    first = {tid: sample(tid) for tid in threads(pid)}
    start = time.perf_counter()
    time.sleep(seconds)
    elapsed = time.perf_counter() - start
    rows = []
    for tid in threads(pid):
        now, before = sample(tid), first.get(tid)
        if not now or not before:
            continue
        rows.append(dict(tid=tid, name=now[1], percentOfCore=100 * (now[0] - before[0]) / elapsed))
    rows.sort(key=lambda row: -row['percentOfCore'])
    return dict(pid=pid, seconds=elapsed, total=sum(r['percentOfCore'] for r in rows), threads=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=5)
    parser.add_argument('--out')
    args = parser.parse_args()
    value = measure(args.pid, args.seconds)
    if args.out:
        open(args.out, 'w', encoding='utf-8').write(json.dumps(value, indent=2))
    print(json.dumps(dict(total=round(value['total'], 1), top=[(r['tid'], r['name'], round(r['percentOfCore'], 1)) for r in value['threads'][:24]]), indent=1))
