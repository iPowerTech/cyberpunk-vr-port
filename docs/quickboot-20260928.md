# Unattended local test startup

User authorization on 2026-09-28 supersedes the earlier manual-launch-only rule:
the assistant may launch Cyberpunk for these tests and must close it after a
completed test session. Never run a RenderDoc replay concurrently with the game.

`mods/cet/CyberpunkVRPort_QuickBoot` was copied from the user-specified restructure
checkout and installed via WolvenKit. It requests `LoadLastCheckpoint(false)`
once after the first main menu. The only logic adjustment cancels an armed resume
when the helper has been disabled. It remains excluded from release packages.

`bin/x64/vrport-launcher.ini` now supports `show_launcher`:

- `0`: skip the configuration window and use saved resolution/HMD/runtime.
- `1`: show the window at startup; this is the default if the key is absent.

The setting is preserved by `SaveLauncherConfig`. The installed file is set to
`0`, with width/height still 2560, HMD type 3 and debug 0. UserSettings and VRIK
calibration hashes did not change. QuickBoot is enabled with a 1.25-second delay.

Validated installed DLL:
`23A4AE303D90B8956667D0A3C804C40A88924DCA46D80567E1F283FC1AD43665`.
Deployment backup and proof files are in `build/quickboot-20260928`.

Validation:

- WolvenKit `launch_game`, `deployRedmod=false`, `extraArgs=-skipStartScreen`:
  PID 14452 skipped the launcher and automatically loaded the last save without
  injected keys. Log confirms 2560x2560/HMD 3 and `menu=0`; CETBridge confirms a
  live player at (3492.7754, -379.6673, 133.1169). QuickBoot status is
  `resumed:LoadLastCheckpoint(false)`. Simulator screenshot also captured.
- With `show_launcher=1`, PID 7692 created a visible
  `CyberpunkVRPortLauncherClass` window, confirmed by process-scoped Win32
  enumeration. That test process was closed and `show_launcher=0` restored.
- Both test processes were terminated after their checks. No automatic looping
  launcher or background test process was left running.

For subsequent tests: confirm no game or RenderDoc replay is running, launch via
WolvenKit with the arguments above, verify fresh QuickBoot state plus an attached
player and gameplay/menu state, run the bounded test batch, then close the exact
launched game PID. A `resumed` file alone only proves the load was requested.

The user also reported an intermittent tiny-window startup and instructed an
immediate close/relaunch instead of testing it. PID 13048 reproduced it: native
game client 1024x768 (819x614 when queried by a DPI-unaware process at 125%). The
game was closed before any renderer probe, then relaunched. Window inspection now
uses physical pixels. GPU/geometry readers reject an unminimized game client at
or below 1280x800; this guard is for the current large-resolution test setup.
On this rejection, close the exact game PID, launch again and recheck size before
collecting data. Simulator-window size is irrelevant; inspect W2ViewportClass.

The small-window startup also occurred on PID 9036; it was closed immediately
after detection. PID 16016 subsequently opened correctly at 2560x1440, showing
the issue is intermittent. At the user's request, unattended startup now waits
`launcher_delay_ms=1000` while pumping window messages (0..10000 ms), and QuickBoot
waits 2.5 seconds after main-menu initialization. Allow at least three seconds
after process exit before the next launch. Keep the small-window rejection/retry
rule even with these waits; a short successful series is not proof of eliminating
an intermittent race.

DLL with startup wait:
`61148BCCE090C4DE17578850FB5C6E13AEF1E323DD0FEC84AEEC5ABA7CB6646E`.
Backup: `build/quickboot-20260928/startup-wait`.
The production wait function passed a hidden-window test of message dispatch,
zero-delay behavior, bounded waiting and preservation of WM_QUIT. First live run
PID 15680 loaded the save automatically, had a physical 2560x1440 game client,
and rendered both eyes normally; it was then closed.

Second and third delayed starts, PIDs 15320 and 23512, also auto-loaded a player
with physical 2560x1440 game clients and no launcher window. Each process was
closed after its test session; the third additionally hosted a bounded render
comparison batch. Three successful starts demonstrate the tested behavior but do
not establish that the intermittent small-window issue can never recur.
Final installed settings remain show_launcher=0, launcher_delay_ms=1000 and
QuickBoot enabled/delay=2.50. Game and RenderDoc are closed.

Latest steering: avoid unnecessary closes. Combine related tests into one game
session. Restart only for an invalid tiny-window launch or a required DLL change;
close after the complete test series, not between every individual comparison.


2026-09-28 continuation: always inspect a fresh simulator screenshot before
arming each live rendering test. A normal window size alone is insufficient
(user correction). The 1024x768 value found in native AppData UserSettings was
changed once to the previously normal 2560x1440 and backed up as
`native-settings-before-window-restore-20260928.json`; this is NOT a proven fix
for the intermittent startup bug, and no automatic settings replacement was
added. The VR resolution remains 2560x2560. Local QuickBoot delay is now 5 seconds.

A player can already exist while the initial loading screen still requires
Continue. PID 28568 showed that prompt in the captured image. A single Space
press was sent only after verifying/focusing that exact game HWND/PID; a new
screenshot then confirmed full gameplay in both eyes. Do not diagnose loading
screen letterboxing as a gameplay rendering failure or restart a process just
because the player exists but the loading/menu state has not finished.


Additional startup evidence: normal physical window size can coexist with a
broken right-eye strip. Such launches (4796, 24916, 11224) were rejected from
render tests after screenshot inspection. Temporarily using show_launcher=1
and the port's verified Start Game button produced normal both-eye gameplay in
10692, 7716, 12504 and 24528; this is a test workaround, not proof of fixing the
startup race. Helpers start_launcher.py and continue_loading.py check the exact
PID/window/button and use a single bounded action. Continue is sent only after
confirming the loading prompt, never blindly in gameplay. At the end of this
series show_launcher=0 was restored; launcher_delay_ms=1000 and local QuickBoot
delay=5.00 remain. Check an actual fresh gameplay image before every test series.
