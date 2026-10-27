// The panel pages UiRec.cpp records, for a test program that links the widgets alone.
#include "Overlay/OverlayInternal.hpp"

namespace overlay {
bool DrawLiveControls(LiveControlsUiState&, int, bool* editing) {
    if (editing) *editing = false;
    return false;
}
bool DrawOverlayPage(LiveControlsUiState&) { return false; }
}
