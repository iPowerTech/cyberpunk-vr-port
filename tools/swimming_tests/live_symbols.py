"""Resolve this launch's plugin symbols from an exactly matching local DLL/PDB.

Toolhelp only enumerates modules. No debugger attachment or game-memory writes.
"""
import argparse, ctypes as c, hashlib, json
from ctypes import wintypes as w
from pathlib import Path

class Module(c.Structure):
    _fields_=[('size',w.DWORD),('id',w.DWORD),('pid',w.DWORD),('globalUses',w.DWORD),
        ('processUses',w.DWORD),('base',c.c_void_p),('bytes',w.DWORD),('handle',w.HMODULE),
        ('name',w.WCHAR*256),('path',w.WCHAR*260)]
class Symbol(c.Structure):
    _fields_=[('size',w.ULONG),('type',w.ULONG),('reserved',c.c_ulonglong*2),
        ('index',w.ULONG),('bytes',w.ULONG),('base',c.c_ulonglong),('flags',w.ULONG),
        ('value',c.c_ulonglong),('address',c.c_ulonglong),('register',w.ULONG),
        ('scope',w.ULONG),('tag',w.ULONG),('length',w.ULONG),('maxLength',w.ULONG),('name',c.c_char*1024)]

def loaded_module(pid,name):
    """Path of the module `name` as the process actually loaded it, or None."""
    k=c.WinDLL('kernel32',use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32NextW.argtypes=k.Module32FirstW.argtypes
    k.CloseHandle.argtypes=[w.HANDLE]
    snap=k.CreateToolhelp32Snapshot(0x18,pid)
    if snap==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
    m=Module();m.size=c.sizeof(m)
    try:
        more=k.Module32FirstW(snap,c.byref(m))
        while more:
            if m.name.lower()==name.lower():return Path(m.path)
            more=k.Module32NextW(snap,c.byref(m))
    finally:k.CloseHandle(snap)
    return None

def resolve(pid,dll,names):
    k=c.WinDLL('kernel32',use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32NextW.argtypes=k.Module32FirstW.argtypes
    k.CloseHandle.argtypes=[w.HANDLE]
    snap=k.CreateToolhelp32Snapshot(0x18,pid)
    if snap==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
    m=Module();m.size=c.sizeof(m);found=None
    try:
        more=k.Module32FirstW(snap,c.byref(m))
        while more:
            if m.name.lower()==dll.name.lower():found=(m.base,m.bytes,Path(m.path));break
            more=k.Module32NextW(snap,c.byref(m))
    finally:k.CloseHandle(snap)
    if not found:raise RuntimeError('Plugin not loaded in this PID')
    base,size,installed=found
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    if digest(dll)!=digest(installed):raise RuntimeError('Local DLL differs from installed image')
    d=c.WinDLL('dbghelp',use_last_error=True);h=c.c_void_p(0x7357494d)
    d.SymInitializeW.argtypes=[c.c_void_p,w.LPCWSTR,w.BOOL]
    d.SymLoadModuleExW.argtypes=[c.c_void_p,w.HANDLE,w.LPCWSTR,w.LPCWSTR,c.c_ulonglong,w.DWORD,c.c_void_p,w.DWORD]
    d.SymLoadModuleExW.restype=c.c_ulonglong
    d.SymFromName.argtypes=[c.c_void_p,c.c_char_p,c.POINTER(Symbol)]
    d.SymCleanup.argtypes=[c.c_void_p]
    d.SymSetOptions(2|4|0x200|0x80000) # undecorate, deferred, exact symbols, no UI
    if not d.SymInitializeW(h,str(dll.parent),False):raise c.WinError(c.get_last_error())
    result={'pid':pid,'module_base':hex(base),'dll':str(dll),'sha256':digest(dll)}
    try:
        if not d.SymLoadModuleExW(h,None,str(dll),None,base,size,None,0):raise c.WinError(c.get_last_error())
        for name in names:
            if '*' in name:
                callbackType=c.WINFUNCTYPE(w.BOOL,c.POINTER(Symbol),w.ULONG,c.c_void_p)
                def collect(ptr,_size,_context):
                    value=ptr.contents
                    if base<=value.address<base+size:
                        result[c.string_at(c.addressof(value)+Symbol.name.offset,value.length).decode()]=hex(value.address)
                    return True
                callback=callbackType(collect)
                d.SymEnumSymbols.argtypes=[c.c_void_p,c.c_ulonglong,c.c_char_p,callbackType,c.c_void_p]
                if not d.SymEnumSymbols(h,base,name.encode(),callback,None):raise c.WinError(c.get_last_error())
                continue
            symbol=Symbol();symbol.size=88;symbol.maxLength=1023
            if not d.SymFromName(h,name.encode(),c.byref(symbol)):raise RuntimeError('Symbol missing: '+name)
            if not base<=symbol.address<base+size:raise RuntimeError('Symbol outside current module: '+name)
            result[name]=hex(symbol.address)
    finally:d.SymCleanup(h)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True)
    p.add_argument('--dll',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('symbols',nargs='+');a=p.parse_args()
    result=resolve(a.pid,a.dll.resolve(),a.symbols)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
