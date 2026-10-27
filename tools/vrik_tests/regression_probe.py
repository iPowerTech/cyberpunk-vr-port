"""Reintroduce old defects in build-only copies; assertions must detect each one.

Never edits live sources or deploys a DLL. All compiler/test output and the exact
mutated sources remain under build/vrik-regression-probe for review.
"""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/vrik-regression-probe"
OUT.mkdir(parents=True, exist_ok=True)


def replace_once(text, old, new):
    assert text.count(old) == 1, f"Source changed; cannot locate regression boundary: {old}"
    return text.replace(old, new, 1)


rig = (ROOT / "src/Anim/CharacterRig.cpp").read_text(encoding="utf-8")
rig = replace_once(rig, "    float viewHead[4],handHead[4],view[4];", """    // Legacy identity-as-invalid sentinel.
    if(viewHeadXr[0]==0 && viewHeadXr[1]==0 && viewHeadXr[2]==0 && viewHeadXr[3]==1) {
        std::copy_n(viewWorldRot,4,outWorldRot);return true;
    }
    float viewHead[4],handHead[4],view[4];""")
rig = replace_once(rig, "VRIK_QuatMul(camModelRot,g_VRHeadReferenceModelRot,headModelRot);",
                   "std::copy_n(camModelRot,4,headModelRot); // Legacy camera axes written directly to skull.")
rig = replace_once(rig, "void VRIK_SyncShadowUpper(uint8_t* boneBuf) {",
                   "void VRIK_SyncShadowUpper(uint8_t* boneBuf) { return; // Legacy: shadow aliases left native.")
snapshot = (ROOT / "src/Anim/HandSnapshot.cpp").read_text(encoding="utf-8")
rig = replace_once(rig,
    "bool VRIK_RestoreMissingCamera(uint8_t* boneBuf,VrikCameraStatus status,uint32_t tick,uint64_t nowMs) {",
    "bool VRIK_RestoreMissingCamera(uint8_t* boneBuf,VrikCameraStatus status,uint32_t tick,uint64_t nowMs) { if(status==VrikCameraStatus::Unavailable)return false;")
rig = replace_once(rig,
    "    std::fill_n(g_VRForeTwistR,3,-1);std::fill_n(g_VRForeTwistL,3,-1);",
    "    // Regression: indices survive a rig without these helpers.")
rig = replace_once(rig, "void VRIK_PinGirdleTranslations(uint8_t* boneBuf) {",
    "void VRIK_PinGirdleTranslations(uint8_t* boneBuf) { return; // Preserve a displaced animated pivot.")
rig = replace_once(rig,
    "void VRIK_UpdateForearmDeformation(uint8_t* boneBuf,int foreIdx,int handIdx,bool left) {",
    "void VRIK_UpdateForearmDeformation(uint8_t* boneBuf,int foreIdx,int handIdx,bool left) { return; // Missing helpers.")
snapshot = replace_once(snapshot, "    if (g_handsStableValid) return g_handsStable[i];\n    return 0.0f;",
                        "    if (g_handsStableValid) return g_handsStable[i];\n    return g_pSharedHands ? g_pSharedHands[i] : 0.0f;")
snapshot = replace_once(snapshot, "    const bool paired=SharedPose(115)==2.0f;",
                        "    const bool paired=false; // Legacy: subtract the newest global consumption.")
rig_path = OUT / "CharacterRig.cpp"
snapshot_path = OUT / "HandSnapshot.cpp"
rig_path.write_text(rig, encoding="utf-8")
snapshot_path.write_text(snapshot, encoding="utf-8")


def logged(command, filename):
    with (OUT / filename).open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Build probe failed: {OUT / filename}")


logged(["cmake", "-S", str(ROOT / "tools/vrik_tests"), "-B", str(OUT / "build"),
        f"-DVR_CHAIN_CHARACTER_SOURCE={rig_path.as_posix()}",
        f"-DVR_CHAIN_SNAPSHOT_SOURCE={snapshot_path.as_posix()}"], "configure.log")
logged(["cmake", "--build", str(OUT / "build"), "--config", "Release"], "build.log")
exe = OUT / "build/Release/vrik_chain_tests.exe"
report = []
for case, expected in {
    "startup_packet": "unfinished first packet exposed a valid hand",
    "neutral_view_reference": "identity view head skipped rebase",
    "head_basis": "head lost reference axes",
    "shadow_upper": "shadow position differs from primary",
    "missing_camera_replay": "missing camera frame did not replay the complete upper pose",
    "paired_consumed": "a later native move shifted the old head packet",
    "rig_rebind": "rig rebind retained old helper or accepted leg topology",
    "shoulder_reference": "animated pose was captured as girdle reference",
    "forearm_roll": "forearm skin does not follow wrist progressively",
    "forearm_solve": "full arm solve did not drive forearm helpers with wrist roll",
}.items():
    result = subprocess.run([str(exe), case], cwd=OUT, capture_output=True, text=True)
    output = result.stdout + result.stderr
    detected = result.returncode != 0 and expected in output
    report.append({"case": case, "exit_code": result.returncode, "detected": detected, "output": output.strip()})
    print(f"{case}: {'DETECTED' if detected else 'MISSED'}", flush=True)
(OUT / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
assert all(item["detected"] for item in report), "A regression was not detected"
