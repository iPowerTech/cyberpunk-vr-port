"""Run the production bridge fixture; requires lupa.luajit21 (no game process)."""
from pathlib import Path
from lupa.luajit21 import LuaRuntime

root = Path(__file__).resolve().parents[2]
lua = LuaRuntime()
lua.globals().arg = lua.table_from([str(root / 'mods/cet/CyberpunkVRPort_Stereo/modules/story_attention.lua')])
lua.execute((root / 'tools/story_attention_tests/bridge.lua').read_text(encoding='utf-8'))
