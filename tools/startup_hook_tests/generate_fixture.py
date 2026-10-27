"""Exercise the production installer with a worker at every patch boundary.

Only Win32 protection/pointer-exchange and D3D12 calls are mocked. Both lookup
and installer bodies are copied verbatim from production. No GPU required.
The regression variant moves publication back behind the detours, reproducing
the old unsafe ordering. It must lose commands under this adversarial schedule.
"""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/Stereo/DeviceHooks.cpp').read_text()
source=source[source.index('static std::array<CommandListVtableHook, 16>'):source.index('// ID3D12Device slots 27 and 29.')]
header=(root/'include/Stereo/StereoInternal.hpp').read_text()
record=header[header.index('struct CommandListVtableHook {'):]
record=record[:record.index('\n};')+3]
types=sorted(set(re.findall(r'\bPFN_\w+',record)))
methods={46:('OMSetRenderTargets','original'),26:('ResourceBarrier','barrier_original'),
 15:('CopyBufferRegion','cbr_original'),59:('ExecuteIndirect','indirect_original'),14:('Dispatch','dispatch_original'),
 21:('RSSetViewports','viewports_original'),22:('RSSetScissorRects','scissor_original'),10:('GfxReset','reset_original'),
 12:('DrawInstanced','draw_original'),13:('DrawIndexedInstanced','drawidx_original'),44:('IASetVertexBuffers','iavb_original'),
 29:('SetComputeRootSignature','crootsig_original'),31:('SetComputeRootDescriptorTable','crootdt_original'),
 25:('SetPipelineState','setpso_original'),47:('ClearDepthStencilView','cleardsv_original')}
prefix='''#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <iostream>
#include <string>
using DWORD=unsigned long;
constexpr DWORD PAGE_READWRITE=4;
struct ID3D12GraphicsCommandList {void** vtable;};
using TestFn=void(*)(ID3D12GraphicsCommandList*);
static unsigned forwarded=0,dropped=0,intercepted=0;
static bool failProtection=false;
static ID3D12GraphicsCommandList* active=nullptr;
void Exercise();
bool VirtualProtect(void*,size_t,DWORD,DWORD* before) {
    Exercise();*before=2;return !failProtection;
}
void* InterlockedExchangePointer(void* volatile* where,void* value) {
    void* previous=*where;*where=value;Exercise();return previous;
}
#define log(...) ((void)0)
void Native(ID3D12GraphicsCommandList*){++forwarded;}
'''
prefix+='\n'.join('using '+name+'=TestFn;' for name in types)+'\n'+record+'\n'
prefix+='const CommandListVtableHook* command_list_hook_entry(ID3D12GraphicsCommandList*);\n'
for slot,(name,field) in methods.items():
 prefix+=f'''void hk_{name}(ID3D12GraphicsCommandList* list) {{
    ++intercepted;const auto* entry=command_list_hook_entry(list);
    if(!entry || !entry->{field}){{++dropped;return;}}
    entry->{field}(list);
}}
'''
suffix='''
void Exercise(){
    if(!active)return;
    const size_t slots[]{'''+','.join(map(str,methods))+'''};
    for(auto slot:slots)reinterpret_cast<TestFn>(active->vtable[slot])(active);
}
int main(int argc,char** argv){
    failProtection=argc>1 && std::string(argv[1])=="failed_protection";
    std::array<void*,64> table;table.fill(reinterpret_cast<void*>(&Native));
    ID3D12GraphicsCommandList list{table.data()};active=&list;
    patch_command_list_vtable(&list);Exercise();
    if(failProtection){
        if(g_command_list_vtable_hook_count.load()!=0 || intercepted)return 2;
        for(auto p:table)if(p!=reinterpret_cast<void*>(&Native))return 3;
    }else{
        if(g_command_list_vtable_hook_count.load()!=1 || !intercepted || forwarded<100)return 4;
        patch_command_list_vtable(&list); // duplicate registration must be idempotent
        if(g_command_list_vtable_hook_count.load()!=1)return 5;
        if(table[16]!=reinterpret_cast<void*>(&Native)||table[17]!=reinterpret_cast<void*>(&Native))return 6;
    }
    std::cout<<"forwarded="<<forwarded<<" intercepted="<<intercepted<<" dropped="<<dropped<<'\\n';
    return dropped?1:0;
}
'''
publication='    g_command_list_vtable_hooks[count]=entry;\n    g_command_list_vtable_hook_count.store(count+1,std::memory_order_release);\n'
assert source.count(publication)==1
regression=source.replace(publication,'').replace('    DWORD ignored{};',publication+'    DWORD ignored{};')
destination=Path(sys.argv[1]);destination.mkdir(exist_ok=True,parents=True)
for name,body in [('current',source),('regression',regression)]:
 (destination/(name+'.cpp')).write_text(prefix+body+suffix)
