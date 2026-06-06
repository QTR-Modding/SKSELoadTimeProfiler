#pragma once

#include <atomic>

// Hot-path asset-read accounting: tracks bytes and time read from BSA archives vs
// raw loose files during a save load. Hooks are vtable detours on
// BSResource::ArchiveStream::DoRead and BSResource::LooseFileStream::DoRead;
// CompressedArchiveStream shares ArchiveStream's read vfunc so one hook covers both
// uncompressed and compressed BSAs.
//
// Timing uses raw TSC cycles (__rdtsc) on the hot path rather than
// std::chrono::high_resolution_clock (QueryPerformanceCounter): the read vfunc fires
// ~200K times per load, so the ~25ns/call QPC cost would itself dominate the result.
// Cycles are converted to ns at snapshot time using a TSC frequency self-calibrated
// from the load window (no startup calibration needed).
namespace AssetReadProfiling {
    struct Stats {
        uint64_t bytes{0};
        uint64_t totalNs{0};  // converted from TSC cycles at snapshot time
        uint64_t calls{0};
    };

    enum class Source : uint8_t { Archive, Loose };

    // Called from the hot-path thunks. Atomic, lock-free. a_cycles is a raw __rdtsc delta.
    void Record(Source src, uint64_t bytes, uint64_t cycles);

    // Reset accumulators and capture the calibration window anchors (QPC + TSC).
    void BeginLoad();

    // Snapshot for the most recently active window; totalNs is derived from accumulated
    // cycles via the window-calibrated TSC frequency.
    Stats SnapshotArchive();
    Stats SnapshotLoose();
}
