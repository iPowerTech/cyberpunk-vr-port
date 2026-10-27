#pragma once
// The simulator's explicit Reset View resets raw poses without a space event.
void PollSimulatorRecenterHook();
void RemoveSimulatorRecenterHook();
