#pragma once

#include <atomic>

// BSA-vs-loose asset-read accounting during a save load (vtable detours on
// ArchiveStream / LooseFileStream::DoRead). The read vfunc fires ~200K times/load, so the
// hot path times with __rdtsc (far cheaper than QPC), converted to ns via a window-calibrated TSC.
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
