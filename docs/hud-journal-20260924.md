# Journal and new-area notifications in the texture HUD

The user observed a new-area notification outside the XR HUD. In PID4636,
WolvenKit found `NewLocationNotification` showing "НОВАЯ ЗОНА" and
"Рэттлснейк-Крик" under `JournalNotificationQueue`. Its parent chain went through
`HUDSlotMiddleWidget / LeftCenter` to the ordinary HUD Base Window. This is
separate from `ZoneAlertNotificationQueue`, which already has a source window.

The game's `prototype_hud.inkhud` identifies the queue as the case-sensitive
HUD entry `Notifications_Journal`, with `useSeparateWindow=false`. Its resource
is `base/gameplay/gui/widgets/notifications/quest_update.inkwidget`. Live root
metadata already provides an authored1300x550 capture rectangle at(0,0), which
contains the current800x317 notification. No Lua resizing or asset modification
is needed.

The existing guarded pre-spawn hook now enables a separate window for that
entry. It routes to Main and therefore inherits the normal HUD follow, depth,
style and visibility policy. The game's queue still owns timing, text, animations
and input actions. The native slot is masked only after the XR consumer is ready,
and restored by the existing fallback logic. Existing notification variants in
the same journal queue follow the same path.

The layout editor includes **Journal / area notifications**, persisted under
`hud_element_journal_notifications`. It is appended to the element catalogue so
existing element indices and setting keys retain their meaning.

All33 HUD CTests passed, including separate-window/Main routing, layout settings
round trips, capture lifetime, XR readiness, follow and the prior loot regression.
Evidence and extracted authored resources are in `build/hud-journal-20260924/`.
The Release build passed and was installed after closing PID4636 at the user's
requested kill preference. Deployment report:
`build/roomscale-plugin-deploy-20260924-165442/deployment.json`.
DLL SHA256: `96588a7fb3158e03946a90fc2a54547a32567e0aa83a377fbd04124252ff5506`.
The existing Lua module remains current. INI hash
`61ec0f36c52f84e519da719c9a8748634a2371afefb6feec860c34a97d847698` and calibration
were preserved. The user restarted into PID15864 for live verification.

For a UI-only replay after restart, call the existing journal queue's
`OnNewLocationDiscovered(true)` callback. It constructs the standard notification
from the current UI_Map blackboard values and does not discover a world location
or modify journal progression. The preceding scene's UI_Map values were
`currentLocation=LocKey#10969`, `currentLocationEnumName=Badlands_BiotechnicaFlats`.
Check that JournalNotificationQueue now has an inkVirtualWindow parent and retains
the authored1300x550 bounds; inspect the visible text and XR output after replay.

## Live result

PID15864 loaded the matching installed DLL. The startup log confirms the separate
window for `Notifications_Journal`; WolvenKit confirmed the queue's new
inkVirtualWindow parent and unchanged1300x550 authored bounds. HUD publication
reported25 sources with no module errors before replay. Calling
`OnNewLocationDiscovered(true)` produced the visible "НОВАЯ ЗОНА" line and raised
the successfully masked source count to26. The user confirmed the notification
works in the HUD. The first simulator screenshot was taken during its intro;
visual acceptance is the user's confirmation, not that early screenshot alone.
Evidence: `build/hud-journal-20260924/live-15864.json` and
`after-simulator.png`. No temporary setting changes or event subscriptions remain.
