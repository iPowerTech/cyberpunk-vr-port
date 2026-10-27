"""Build-only mutations verify that the identity tests reject old failure modes."""
from pathlib import Path
import json,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build/pose-id-regression-probe';overlay=out/'include'
changes={
    'Camera/PoseAddressLedger.hpp': [
        ('source->second.generation!=receipt.generation ||\n           source->second.fingerprint!=sourceNow',
         'false ||\n           source->second.fingerprint!=sourceNow'),
        ('        // A cached VRCAM descriptor can remain alive while thousands of MAIN',
         '''        // Reproduce the previous publication-ring expiration in a build-only copy.
        const auto slot=m_probeNext++%Capacity;
        if(const auto old=m_records.find(m_probeKeys[slot]);old!=m_records.end() &&
           old->second.generation==m_probeGenerations[slot])m_records.erase(old);
        m_probeKeys[slot]=address;m_probeGenerations[slot]=m_generation+1;
        // A cached VRCAM descriptor can remain alive while thousands of MAIN'''),
        ('    mutable uint64_t m_lastUse{};',
         '    mutable uint64_t m_lastUse{};\n    std::array<uintptr_t,Capacity> m_probeKeys{};\n    std::array<uint64_t,Capacity> m_probeGenerations{};\n    uint64_t m_probeNext{};'),
    ],
    'Camera/ImagePoseLedger.hpp': [('std::lock_guard lock(m_mutex);m_lists[list].clear();','std::lock_guard lock(m_mutex); // old recording retained')],
    'Runtimes/CaptureBufferLeases.hpp': [('if(value.readers || value.writing)return false;','if(value.writing)return false;')],
}
for name,replacements in changes.items():
    value=(root/'include'/name).read_text(encoding='utf-8')
    for before,after in replacements:
        assert value.count(before)==1,name
        value=value.replace(before,after,1)
    path=overlay/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(value,encoding='utf-8')
def logged(args,name):
    with (out/name).open('w',encoding='utf-8') as f:
        result=subprocess.run(args,cwd=root,stdout=f,stderr=subprocess.STDOUT)
    assert result.returncode==0,name
logged(['cmake','-S',str(root/'tools/pose_identity_tests'),'-B',str(out/'build'),
        f'-DPOSE_ID_INCLUDE_OVERLAY={overlay.as_posix()}'],'configure.log')
logged(['cmake','--build',str(out/'build'),'--config','Release'],'build.log')
exe=out/'build/Release/pose_identity_tests.exe';results=[]
for case,message in {
    'owner_generation':'reused source address accepted an obsolete receipt',
    'changed_during_copy':'same coordinates hid a source publication during copy',
    'reset_before_submit':'discarded command list published a pose',
    'reservations':'producer can replace pixels selected by the consumer',
    'cached_camera_lifetime':'unrelated MAIN publications expired a cached VRCAM pose',
}.items():
    result=subprocess.run([str(exe),case],capture_output=True,text=True,cwd=root)
    output=result.stdout+result.stderr;detected=result.returncode!=0 and message in output
    results.append({'case':case,'detected':detected,'output':output.strip()});print(case,detected,flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert all(r['detected'] for r in results)
