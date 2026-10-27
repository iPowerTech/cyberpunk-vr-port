#include "Overlay/DrawWork.hpp"
#include <stdexcept>
#include <iostream>

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() try {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.0f / 90;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Check(!overlay::HasDrawWork(nullptr), "null frame accepted");

    // A closed menu still runs NewFrame/Render to retire last frame's lists.
    // Exercise transitions, including the background list used for laser dots.
    for (int frame = 0; frame < 120; ++frame) {
        ImGui::NewFrame();
        auto* background = ImGui::GetBackgroundDrawList();
        if (frame % 4 == 1) background->AddCircleFilled({50, 50}, 3, IM_COL32_WHITE);
        if (frame % 4 == 2) background->AddCallback([](const ImDrawList*, const ImDrawCmd*) {}, nullptr);
        if (frame % 4 == 3) {
            ImGui::SetNextWindowPos({20, 20});
            ImGui::SetNextWindowSize({300, 200});
            ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::Button("Close");
            ImGui::End();
        }
        ImGui::Render();
        auto* draw = ImGui::GetDrawData();
        Check(draw && draw->Valid, "CPU frame not completed");
        Check(overlay::HasDrawWork(draw) == (frame % 4 != 0), "empty/dot/callback/menu transition lost");
        if (frame % 4 == 0) Check(draw->TotalVtxCount == 0, "stale geometry after closing");
        if (frame % 4 == 2) Check(draw->TotalVtxCount == 0, "callback-only coverage requires no geometry");
    }
    ImGui::DestroyContext();
    std::cout << "PASS 120 empty/dot/callback/menu transitions\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
