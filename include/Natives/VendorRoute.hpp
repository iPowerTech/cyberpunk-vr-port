#pragma once
#include <RED4ext/RED4ext.hpp>

namespace cvr::vendor_route {
void WatchdogRegistered(bool);
// Main-thread watchdog: return an unacknowledged request to the native UI.
bool Update(RED4ext::CGameApplication*);
bool Reset(RED4ext::CGameApplication*);
}
