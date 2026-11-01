// XrGpuTimeline -- see Runtimes/XrGpuTimeline.hpp. Nothing here changes what is captured or submitted; FSR3's
// scheduling reads the capture stamps (CaptureFinishedMs).
#include "Runtimes/XrGpuTimeline.hpp"

#include <windows.h>
#include <atomic>
#include <mutex>

extern void Log(const char* fmt, ...);
extern "C" int CyberpunkVR_XrRateLog;

namespace cvr::xrtimeline {
namespace {

constexpr UINT kSlots = 32;                 // per ring: captures by serial, copies by cycle
constexpr UINT kCopyBase = kSlots * 2;      // query index of the first copy stamp
constexpr UINT kQueries = kSlots * 4;

std::mutex g_initMutex;
std::atomic<bool> g_ready{false};
bool g_failed = false;
ID3D12QueryHeap* g_heap = nullptr;
ID3D12Resource* g_readback = nullptr;
ID3D12CommandQueue* g_queue = nullptr;

double g_qpcToMs = 0;
// GPU tick -> CPU ms, from the queue's clock calibration (refreshed by Poll).
std::atomic<double> g_gpuToMs{0}, g_offsetMs{0};
UINT64 g_gpuFreq = 0;

// Captures, by serial % kSlots: the serial the slot holds and when its list was submitted (CPU ms).
std::atomic<uint64_t> g_capSerial[kSlots];
// Copies, by cycle % kSlots: the cycle, the capture it copied, when it was submitted.
uint64_t g_copyCycle[kSlots] = {}, g_copySerial[kSlots] = {};
double g_copySubmitMs[kSlots] = {};
uint64_t g_cycle = 0, g_pendingSerial = 0;   // XR thread only

std::mutex g_sumMutex;
double g_sumQueued = 0, g_sumAfterCapture = 0, g_sumBetween = 0, g_sumCopyGpu = 0, g_maxCopyGpu = 0;
unsigned g_count = 0, g_countCapture = 0;

double NowMs() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return double(c.QuadPart) * g_qpcToMs; }

void Calibrate() {
    UINT64 gpu = 0, cpu = 0;
    if (!g_queue || FAILED(g_queue->GetClockCalibration(&gpu, &cpu)) || !g_gpuFreq) return;
    const double toMs = 1000.0 / double(g_gpuFreq);
    g_gpuToMs.store(toMs, std::memory_order_relaxed);
    g_offsetMs.store(double(cpu) * g_qpcToMs - double(gpu) * toMs, std::memory_order_relaxed);
}

double GpuMs(UINT64 ticks) { return double(ticks) * g_gpuToMs.load(std::memory_order_relaxed) + g_offsetMs.load(std::memory_order_relaxed); }

bool Init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (g_ready.load(std::memory_order_acquire)) return true;
    std::lock_guard lock(g_initMutex);
    if (g_ready.load(std::memory_order_relaxed)) return true;
    if (g_failed || !device || !queue) return false;
    g_failed = true;
    LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); g_qpcToMs = 1000.0 / double(f.QuadPart);
    if (FAILED(queue->GetTimestampFrequency(&g_gpuFreq)) || !g_gpuFreq) return false;
    D3D12_QUERY_HEAP_DESC hd{}; hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; hd.Count = kQueries;
    if (FAILED(device->CreateQueryHeap(&hd, IID_PPV_ARGS(&g_heap)))) return false;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = kQueries * 8; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&g_readback)))) return false;
    g_queue = queue;
    g_queue->AddRef();
    Calibrate();
    g_failed = false;
    g_ready.store(true, std::memory_order_release);
    return true;
}

}  // namespace

bool Enabled() { return CyberpunkVR_XrRateLog != 0; }

// The capture stamps are taken with the report off too: FSR3's scheduling reads when a capture finished
// (CaptureFinishedMs). Two timestamps and a resolve a frame.
void CaptureBegin(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list, uint64_t serial) {
    if (!list || !Init(device, queue)) return;
    list->EndQuery(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, UINT(serial % kSlots) * 2);
}

void CaptureEnd(ID3D12GraphicsCommandList* list, uint64_t serial) {
    if (!list || !g_ready.load(std::memory_order_acquire)) return;
    const UINT first = UINT(serial % kSlots) * 2;
    list->EndQuery(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, first + 1);
    list->ResolveQueryData(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, first, 2, g_readback, UINT64(first) * 8);
    g_capSerial[serial % kSlots].store(serial, std::memory_order_relaxed);
}

double CaptureFinishedMs(uint64_t serial) {
    if (!serial || !g_ready.load(std::memory_order_acquire)) return 0;
    const UINT slot = UINT(serial % kSlots);
    if (g_capSerial[slot].load(std::memory_order_relaxed) != serial) return 0;
    static uint64_t s_reads = 0;
    if (++s_reads % 30 == 0) Calibrate();   // the clocks drift apart: as Poll does, with the report on
    const D3D12_RANGE range{(slot * 2 + 1) * 8, (slot * 2 + 2) * 8};
    void* data = nullptr;
    if (FAILED(g_readback->Map(0, &range, &data))) return 0;
    const UINT64 end = static_cast<const UINT64*>(data)[slot * 2 + 1];
    const D3D12_RANGE none{0, 0};
    g_readback->Unmap(0, &none);
    return end ? GpuMs(end) : 0;
}

void CopyBegin(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
    if (!Enabled() || !list || !Init(device, queue)) return;
    list->EndQuery(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, kCopyBase + UINT(g_cycle % kSlots) * 2);
}

void CopyEnd(ID3D12GraphicsCommandList* list, uint64_t serial) {
    if (!Enabled() || !list || !g_ready.load(std::memory_order_acquire)) return;
    const UINT first = kCopyBase + UINT(g_cycle % kSlots) * 2;
    list->EndQuery(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, first + 1);
    list->ResolveQueryData(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, first, 2, g_readback, UINT64(first) * 8);
    g_pendingSerial = serial ? serial : 1;
}

void CopySubmitted() {
    if (!g_pendingSerial) return;
    const UINT slot = UINT(g_cycle % kSlots);
    g_copyCycle[slot] = g_cycle + 1;   // 0 = empty
    g_copySerial[slot] = g_pendingSerial;
    g_copySubmitMs[slot] = NowMs();
    g_pendingSerial = 0;
    ++g_cycle;
}

void Poll() {
    if (!Enabled() || !g_ready.load(std::memory_order_acquire) || g_cycle < 8) return;
    const uint64_t cycle = g_cycle - 8;
    const UINT slot = UINT(cycle % kSlots);
    if (g_copyCycle[slot] != cycle + 1) return;
    g_copyCycle[slot] = 0;
    static uint64_t s_calibrated = 0;
    if (cycle - s_calibrated >= 30) { s_calibrated = cycle; Calibrate(); }
    const D3D12_RANGE range{0, kQueries * 8};
    void* data = nullptr;
    if (FAILED(g_readback->Map(0, &range, &data))) return;
    const auto* t = static_cast<const UINT64*>(data);
    const UINT64 copyStart = t[kCopyBase + slot * 2], copyEnd = t[kCopyBase + slot * 2 + 1];
    const double submitMs = g_copySubmitMs[slot];
    const uint64_t serial = g_copySerial[slot];
    double afterCapture = 0, between = 0;
    bool haveCapture = false;
    const bool ok = copyStart && copyEnd >= copyStart;
    if (ok) {
        const double startMs = GpuMs(copyStart);
        const UINT capSlot = UINT(serial % kSlots);
        if (g_capSerial[capSlot].load(std::memory_order_relaxed) == serial && t[capSlot * 2 + 1]) {
            afterCapture = startMs - GpuMs(t[capSlot * 2 + 1]);
            haveCapture = true;
        }
        for (UINT i = 0; i < kSlots; ++i) {
            const UINT64 end = t[i * 2 + 1];
            if (!end || !g_capSerial[i].load(std::memory_order_relaxed)) continue;
            const double endMs = GpuMs(end);
            if (endMs > submitMs && endMs <= startMs) between += 1;
        }
        std::lock_guard lock(g_sumMutex);
        g_sumQueued += startMs - submitMs;
        g_sumBetween += between;
        const double copyGpu = GpuMs(copyEnd) - startMs;   // the headset's copy list itself, start to end
        g_sumCopyGpu += copyGpu;
        if (copyGpu > g_maxCopyGpu) g_maxCopyGpu = copyGpu;
        ++g_count;
        if (haveCapture) { g_sumAfterCapture += afterCapture; ++g_countCapture; }
    }
    const D3D12_RANGE none{0, 0};
    g_readback->Unmap(0, &none);
}

void Report() {
    if (!Enabled() || !g_ready.load(std::memory_order_acquire)) return;
    std::lock_guard lock(g_sumMutex);
    if (!g_count) return;
    Log("[xrgpu] %u copies | started %.2f ms after submit | %.2f ms after their capture finished (%u) | "
        "game frames finished on the GPU between submit and start %.2f | the list on the GPU %.2f ms, max %.2f\n",
        g_count, g_sumQueued / g_count, g_countCapture ? g_sumAfterCapture / g_countCapture : 0.0, g_countCapture,
        g_sumBetween / g_count, g_sumCopyGpu / g_count, g_maxCopyGpu);
    g_sumQueued = g_sumAfterCapture = g_sumBetween = g_sumCopyGpu = g_maxCopyGpu = 0;
    g_count = g_countCapture = 0;
}

}  // namespace cvr::xrtimeline
