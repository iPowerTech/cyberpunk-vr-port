#pragma once
#include <imgui.h>

namespace overlay {
// Callback-only lists also need submission, even when they have no vertices.
inline bool HasDrawWork(const ImDrawData* draw) {
    return draw && draw->Valid && draw->DisplaySize.x > 0 && draw->DisplaySize.y > 0
        && draw->CmdListsCount > 0;
}
}
