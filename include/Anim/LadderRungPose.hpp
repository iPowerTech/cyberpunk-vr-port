#pragma once
// Approved rung pose, baked from vrport.ini on 2026-09-22.
// Source values: tools/vrik_tests/fixtures/ladder/tuned-rung.json.
namespace cvr::ladder::profile {
inline constexpr float RungCurl[5]={1.0f,1.0f,1.0f,1.0f,1.0f};
inline constexpr float RungAdjust[2][5][6]={
    { // left
        {10.5f,68.8f,0.0f,-31.5f,29.8f,18.1f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    },
    { // right
        {10.5f,68.8f,0.0f,-31.5f,29.8f,18.1f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
        {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    },
};
}
