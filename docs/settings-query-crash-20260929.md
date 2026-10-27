# Invalid settings query during development

Report: `Cyberpunk2077-20260929-234441-29356-16148`.

The fatal assert was `it != m_options.End()` in `inGameConfigGroup.cpp:76`:
`Could not find in-game config var 'DLAA' in group '/graphics/presets'`.

This was caused by an interactive diagnostic query, not by the foveation host.
The query attempted to discover settings by trying several names through
`GetVar`, including `DLAA`. This native getter asserts on an absent key; Lua
`pcall` cannot catch that process-terminating assert.

Never probe guessed setting names or paths with `GetVar`. Confirm them from
the current UserSettings.json or enumerate the settings API first. This game's
confirmed entries are `/graphics/presets/DLSS` and
`/graphics/presets/ResolutionScaling`; there is no separate `DLAA` variable.
No persistent mod code used the invalid query. The report was retained and
CrashReporter was closed after diagnosis.
