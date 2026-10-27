#pragma once
// UiRec -- the settings panels, read out as data for the in-world VR menu.
//
// The panels (OverlayPanels.cpp, VrWidgets.cpp, the OVERLAY page of VrStyle.cpp) are written once, as
// ImGui code. They call UI:: instead of ImGui::, and UI:: is ImGui itself -- except during a RECORD
// PASS, when every checkbox, slider, combo, button, header, text and tooltip is written down as an item
// instead of drawn, and an edit the VR menu sent for an item is applied the way a click or a drag would
// have been. So the VR menu shows exactly what the panel shows, labels, ranges, disabled states and
// tooltips included, and a new control in the panel appears there with no second copy of anything.
//
// The pass runs on the render thread, at the top of OverlayRender, where the ImGui panel itself runs:
// the panels' statics and the globals they read are never touched from two threads. The CET side asks
// for a page (Request) and sends edits (QueueEdit); the next present records that page, applies the
// edits through the panel code, persists a change exactly as the panel does (SetLiveControlsUiState
// with the ini), and publishes the page as one string (Latest).
//
// THE PAGE STRING: items separated by \x1e, fields by \x1f:
//   kind, id, label, value, min, max, text, flags, tooltip, indent
// kinds: H header, T text, C checkbox, S float slider, I int slider, O combo, B button, P tabs,
//        N number field, G progress, K binding row (label = input, text = action), X separator.
// text: a slider's value as the panel formats it, a combo's or tabs' options joined by \x1d.
// flags: 1 disabled-style text, 2 wrapped text, 4 coloured text (value = 0xRRGGBB), 8 header open by
//        default, 16 item disabled.
// The first item is the page header: kind "#", value = the page, min = the sequence number.

#include <imgui.h>
#include <cfloat>
#include <string>

namespace cvr::uirec {

// The pages, in the panel's own tab order.
inline constexpr int kPageCount = 7;
extern const char* const kPageNames[kPageCount];

bool Recording();

// Render thread: record the requested page, if one was asked for or an edit is waiting.
void RunPendingPass();

// Any thread (the CET natives).
void Request(int page);
void QueueEdit(int page, const char* id, double value);
std::string Latest(int page);

// The recorder's side of the widgets; each returns what the widget would have (changed / clicked).
bool Checkbox(const char* label, bool* value);
bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* format);
bool SliderInt(const char* label, int* value, int minimum, int maximum, const char* format);
bool Combo(const char* label, int* selected, const char* const* items, int count);
bool Tabs(const char* id, int& selected, const char* const* labels, int count);
bool Button(const char* label);
bool InputInt(const char* label, int* value, int step);
bool Header(const char* label, bool defaultOpen);
void Text(int flags, unsigned color, const char* text);
void Binding(const char* input, const char* action);
void Progress(float fraction);
void Separator();
void Tooltip(const char* text);
void BeginDisabled(bool disabled);
void EndDisabled();
void PushID(const char* id);
void PushID(int id);
void PopID();
void Indent(int delta);

}  // namespace cvr::uirec

// ImGui, or the recorder: the panels call these.
namespace UI {
bool Checkbox(const char* label, bool* value);
bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* format = "%.3f",
                 ImGuiSliderFlags flags = 0);
bool InputInt(const char* label, int* value, int step = 1, int fastStep = 100, ImGuiInputTextFlags flags = 0);
bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);
bool BeginTabBar(const char* id, ImGuiTabBarFlags flags = 0);
void EndTabBar();
bool BeginTabItem(const char* label, bool* open = nullptr, ImGuiTabItemFlags flags = 0);
void EndTabItem();
void Text(const char* format, ...) IM_FMTARGS(1);
void TextDisabled(const char* format, ...) IM_FMTARGS(1);
void TextWrapped(const char* format, ...) IM_FMTARGS(1);
void TextColored(const ImVec4& color, const char* format, ...) IM_FMTARGS(2);
void TextUnformatted(const char* text, const char* end = nullptr);
void SetTooltip(const char* format, ...) IM_FMTARGS(1);
bool IsItemHovered(ImGuiHoveredFlags flags = 0);
bool IsItemActive();
bool IsAnyItemActive();
void BeginDisabled(bool disabled = true);
void EndDisabled();
void PushID(const char* id);
void PushID(int id);
void PopID();
void Separator();
void SameLine(float offset = 0.0f, float spacing = -1.0f);
void Spacing();
void Indent(float width = 0.0f);
void Unindent(float width = 0.0f);
void SetNextItemWidth(float width);
void ProgressBar(float fraction, const ImVec2& size = ImVec2(-FLT_MIN, 0), const char* overlay = nullptr);
void PlotLines(const char* label, const float* values, int count, int offset = 0, const char* overlay = nullptr,
               float minimum = FLT_MAX, float maximum = FLT_MAX, ImVec2 size = ImVec2(0, 0), int stride = sizeof(float));
ImGuiIO& GetIO();
}  // namespace UI
