"""Build the stereo plugin and tell a long compile from a hung one.

A header most of the plugin includes (LiveControls.hpp, VrikState.hpp) makes MSBuild recompile nearly
every file, and with /MP one cl.exe can run for several minutes without printing a line -- which looks
exactly like a hang. This watches the build tree's CPU time as well as its output: as long as either
moves the build is alive, and a heartbeat says so every 30 s. Only when neither has moved for --stall
seconds is it called hung; the tree is then listed, killed and the build started again (--retries).

    py -3.13 tools/build/build_stereo.py [--stall 120] [--retries 1] [--target cyberpunkvrport_stereo]

Exit code: the build's own, or 3 when it hung on every attempt.
"""
import argparse
import ctypes
import subprocess
import sys
import threading
import time
from ctypes import wintypes as W
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# The build tree: what MSBuild runs to compile and link, and what it leaves holding locks when it dies.
TOOLS = {'cl.exe', 'link.exe', 'msbuild.exe', 'cmake.exe', 'tracker.exe', 'vctip.exe', 'mspdbsrv.exe',
         'cvtres.exe', 'lib.exe', 'rc.exe', 'dxc.exe'}

k32 = ctypes.WinDLL('kernel32', use_last_error=True)


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ProcessID', W.DWORD),
                ('th32DefaultHeapID', ctypes.c_size_t), ('th32ModuleID', W.DWORD), ('cntThreads', W.DWORD),
                ('th32ParentProcessID', W.DWORD), ('pcPriClassBase', ctypes.c_long), ('dwFlags', W.DWORD),
                ('szExeFile', W.WCHAR * 260)]


k32.CreateToolhelp32Snapshot.restype = W.HANDLE
k32.OpenProcess.restype = W.HANDLE
k32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k32.GetProcessTimes.argtypes = [W.HANDLE] + [ctypes.POINTER(W.FILETIME)] * 4


def processes():
    """{pid: (name, parent)} for every process."""
    snap = k32.CreateToolhelp32Snapshot(2, 0)
    out = {}
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    ok = k32.Process32FirstW(snap, ctypes.byref(entry))
    while ok:
        out[entry.th32ProcessID] = (entry.szExeFile.lower(), entry.th32ParentProcessID)
        ok = k32.Process32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return out


def cpu_seconds(pid):
    h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return 0.0
    c, e, kt, ut = W.FILETIME(), W.FILETIME(), W.FILETIME(), W.FILETIME()
    ok = k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
    k32.CloseHandle(h)
    if not ok:
        return 0.0
    ft = lambda f: (f.dwHighDateTime << 32 | f.dwLowDateTime) / 1e7
    return ft(kt) + ft(ut)


def build_tree(root_pid):
    """The build's processes: the root's descendants, plus build tools running anywhere (MSBuild nodes
    and cl.exe are not always children of the process that started them)."""
    procs = processes()
    tree = {root_pid}
    grew = True
    while grew:
        grew = False
        for pid, (_, parent) in procs.items():
            if parent in tree and pid not in tree:
                tree.add(pid)
                grew = True
    tree |= {pid for pid, (name, _) in procs.items() if name in TOOLS}
    return {pid: procs[pid][0] for pid in tree if pid in procs}


def kill(tree):
    for pid in tree:
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True)


def attempt(cmd, stall):
    start = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding='utf-8', errors='replace', bufsize=1)
    last_output = [time.monotonic()]

    def pump():
        for line in proc.stdout:
            last_output[0] = time.monotonic()
            sys.stdout.write(line)
            sys.stdout.flush()

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    # Per process: a compiler that exits takes its CPU time with it, so a sum would fall and a new,
    # silent link would look idle until it outgrew the compile before it.
    seen, last_progress, last_beat = {}, time.monotonic(), time.monotonic()
    while proc.poll() is None:
        time.sleep(5)
        tree = build_tree(proc.pid)
        times = {pid: cpu_seconds(pid) for pid in tree}
        cpu = sum(times.values())
        now = time.monotonic()
        busy = any(pid not in seen or t > seen[pid] + 0.2 for pid, t in times.items())
        seen = times
        last_progress = max(now if busy else last_progress, last_output[0])
        if now - last_beat >= 30:
            compilers = sorted(n for n in tree.values() if n in ('cl.exe', 'link.exe'))
            print('[build-watch] %.0f s: alive, build CPU %.0f s, %s, quiet %.0f s' % (
                now - start, cpu, ', '.join(compilers) or 'no compiler running', now - last_output[0]), flush=True)
            last_beat = now
        if now - last_progress >= stall:
            print('[build-watch] HUNG: no output and no CPU for %.0f s. Build tree:' % (now - last_progress), flush=True)
            for pid, name in sorted(tree.items(), key=lambda kv: kv[1]):
                print('    %-14s pid %-6d cpu %.1f s' % (name, pid, cpu_seconds(pid)), flush=True)
            kill(tree)
            proc.wait(timeout=30)
            return None
    reader.join(timeout=5)
    print('[build-watch] finished in %.0f s, exit %d' % (time.monotonic() - start, proc.returncode), flush=True)
    return proc.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--target', default='cyberpunkvrport_stereo')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--parallel', default='8')
    ap.add_argument('--build-dir', default='build')
    ap.add_argument('--stall', type=float, default=120.0, help='seconds without output AND without CPU')
    ap.add_argument('--retries', type=int, default=1)
    a = ap.parse_args()
    cmd = ['cmake', '--build', a.build_dir, '--config', a.config, '--target', a.target, '--parallel', a.parallel]
    for n in range(a.retries + 1):
        if n:
            print('[build-watch] retry %d of %d' % (n, a.retries), flush=True)
        code = attempt(cmd, a.stall)
        if code is not None:
            return code
    return 3


if __name__ == '__main__':
    sys.exit(main())
