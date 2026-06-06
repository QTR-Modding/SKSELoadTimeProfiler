#pragma once

#include <atomic>

// Hot-path asset-read accounting: tracks bytes and time read from BSA archives vs
// raw loose files during a save load. Hooks are vtable detours on
// BSResource::ArchiveStream::DoRead and BSResource::LooseFileStream::DoRead;
// CompressedArchiveStream shares ArchiveStream's read vfunc so one hook covers both
// uncompressed and compressed BSAs.
namespace AssetReadProfiling {
    struct Stats {
        uint64_t bytes{0};
        uint64_t totalNs{0};
        uint64_t calls{0};
    };

    enum class Source : uint8_t { Archive, Loose };

    // Called from the hot-path thunks. Atomic, lock-free.
    void Record(Source src, uint64_t bytes, uint64_t ns);

    // Reset accumulators (called at the start of each save load).
    void BeginLoad();

    // Snapshot for the most recently active window. Returned struct copies are atomic
    // loads; callers see a self-consistent view-per-field but not across fields, which
    // is fine for display.
    Stats SnapshotArchive();
    Stats SnapshotLoose();
}
