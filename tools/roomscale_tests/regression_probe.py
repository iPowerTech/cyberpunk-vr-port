"""Prove that live-derived pose regressions fail with the previous algorithms.

Only build-directory copies are mutated; no game files or production sources
are changed. Compiler output and the exact mutant headers remain reviewable.
"""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/pose-chain-regression-probe"
OVERLAY = OUT / "include/Camera"
OVERLAY.mkdir(parents=True, exist_ok=True)

mutations = {
    "EyeCentreLedger.hpp": [
        ("!SameFloatCentre(candidate,head,eyeOrCentre)", "candidate!=head"),
    ],
    "RenderedPoseHistory.hpp": [
        ("    return error<=3.0461742e-10;",
         "    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];"),
        ("const double tolerance=LabelPositionTolerance(position);","const double tolerance=.00001;"),
    ],
    "NativeCameraPair.hpp": [
        ("float(int64_t(centre[k])-entity[k])/131072.0f",
         "float(centre[k])/131072.0f-float(entity[k])/131072.0f"),
        ("float(int64_t(bodyBase[k])-entity[k])/131072.0f",
         "float(bodyBase[k])/131072.0f-float(entity[k])/131072.0f"),
    ],
}
for name, replacements in mutations.items():
    source = (ROOT / "include/Camera" / name).read_text(encoding="utf-8")
    for old, new in replacements:
        assert source.count(old) == 1, f"Regression boundary changed: {name}: {old}"
        source = source.replace(old, new, 1)
    (OVERLAY / name).write_text(source, encoding="utf-8")


def logged(command, name):
    with (OUT / name).open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Regression probe build failed: {OUT / name}")


logged(["cmake", "-S", str(ROOT / "tools/roomscale_tests"), "-B", str(OUT / "build"),
        f"-DROOMSCALE_TEST_INCLUDE_OVERLAY={(OUT / 'include').as_posix()}"], "configure.log")
logged(["cmake", "--build", str(OUT / "build"), "--config", "Release"], "build.log")
exe = OUT / "build/Release/roomscale_tests.exe"
report = []
for case, expected in {
    "eye_float_alias": "live float alias must not expire the Lua camera",
    "render_roundoff": "actual VRCAM matrix round-trip lost the rendered pose",
    "native_camera_pair": "native relative height must not subtract rounded world floats",
    "render_quantized_labels": "one world-float quantum made equivalent image labels ambiguous",
}.items():
    result = subprocess.run([str(exe), case], cwd=OUT, capture_output=True, text=True)
    output = result.stdout + result.stderr
    detected = result.returncode != 0 and expected in output
    report.append({"case": case, "exit_code": result.returncode, "detected": detected,
                   "output": output.strip()})
    print(f"{case}: {'DETECTED' if detected else 'MISSED'}", flush=True)
(OUT / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
assert all(item["detected"] for item in report), "A regression was not detected"
