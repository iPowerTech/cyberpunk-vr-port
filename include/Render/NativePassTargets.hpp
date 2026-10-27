#pragma once
#include <array>
#include <cstdint>

namespace cvr::stereo::pass_targets {
struct View {
    uint64_t identity{},resource{},width{};
    uint32_t height{},format{},viewFormat{},resourceFlags{},viewFlags{},reserved{};
};
static_assert(sizeof(View)==48);
struct Record {
    uint64_t sequence{},scope{};
    uint32_t stage{},frame{},status{},colors{};
    std::array<View,9> views; // RTV slots 0..7, DSV in slot 8.
};
static_assert(sizeof(Record)==464);
}
