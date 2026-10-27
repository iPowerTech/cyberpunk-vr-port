"""Inspect existing simulator Win32 menu commands without activating its window."""
import ctypes as c
from ctypes import wintypes as w
import argparse
import json

class MenuItem(c.Structure):
    _fields_=[('size',w.UINT),('mask',w.UINT),('type',w.UINT),('state',w.UINT),('id',w.UINT),
              ('submenu',w.HMENU),('checked',w.HBITMAP),('unchecked',w.HBITMAP),('data',c.c_size_t),
              ('text',w.LPWSTR),('length',w.UINT),('bitmap',w.HBITMAP)]

def inspect(pid):
    user=c.WinDLL('user32',use_last_error=True)
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback,w.LPARAM]
    user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    user.GetMenu.argtypes=[w.HWND]; user.GetMenu.restype=w.HMENU
    user.GetMenuItemCount.argtypes=[w.HMENU]
    user.GetMenuItemInfoW.argtypes=[w.HMENU,w.UINT,w.BOOL,c.POINTER(MenuItem)]
    result=[]
    def walk(menu):
        items=[]
        for i in range(user.GetMenuItemCount(menu)):
            text=c.create_unicode_buffer(1024)
            item=MenuItem(); item.size=c.sizeof(item); item.mask=0x40|0x04|0x02|0x01
            item.text=c.cast(text,w.LPWSTR); item.length=1023
            if user.GetMenuItemInfoW(menu,i,True,c.byref(item)):
                row={'id':item.id,'text':text.value,'state':item.state}
                if item.submenu:row['items']=walk(item.submenu)
                items.append(row)
        return items
    @callback
    def collect(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value==pid:
            title=c.create_unicode_buffer(1024);user.GetWindowTextW(hwnd,title,len(title))
            if 'simulator' in title.value.lower():
                result.append({'hwnd':int(hwnd),'title':title.value,'items':walk(user.GetMenu(hwnd))})
        return True
    user.EnumWindows(collect,0)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True)
    print(json.dumps(inspect(p.parse_args().pid),indent=2,ensure_ascii=True))
