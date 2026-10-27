"""Reject the known tiny-window startup before arming renderer experiments."""
import ctypes as c
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'quickboot_tests'))
import launch_to_save as boot

def validate_windows(rows):
    windows=[w for w in rows if w['class']==boot.GAME_CLASS]
    if not windows or any(w['class']==boot.LAUNCHER_CLASS and w['visible'] for w in rows):
        raise RuntimeError('Game window is not ready; no renderer probe was armed')
    if any(not w['minimized'] and 0<w['width']<=1280 and 0<w['height']<=800 for w in windows):
        raise RuntimeError('Known small-window startup: close this game PID and relaunch before testing')
    return [{'client':[w['width'],w['height']],'minimized':w['minimized']} for w in windows]

def require_normal_window(pid):
    boot.user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    return validate_windows(boot.windows_for(pid))
