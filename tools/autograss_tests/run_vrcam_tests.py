"""Run the production VRCAM lifecycle module in a mocked CET/LuaJIT environment.

Requires lupa (pip install --target build/lua-test-deps lupa).
Optional argument: an older vrcam_select.lua to check the regression fixture.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "build/lua-test-deps"))
from lupa.luajit21 import LuaRuntime

module = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "mods/cet/CyberpunkVRPort_Stereo/modules/vrcam_select.lua"
fixture = Path(__file__).with_name("vrcam_loading_fixture.lua").read_text(encoding="utf-8")
assert fixture.count("-- VRCAM_MODULE") == 1
script = fixture.replace("-- VRCAM_MODULE", module.read_text(encoding="utf-8"))
LuaRuntime().execute(script)
