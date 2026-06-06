#include "AssetReadProfiling.h"

#include <atomic>

namespace {
    struct Atomic {
        std::atomic<uint64_t> bytes{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> calls{0};
    };
    Atomic g_archive;
    Atomic g_loose;

    AssetReadProfiling::Stats LoadStats(const Atomic& a) {
        return AssetReadProfiling::Stats{
            a.bytes.load(std::memory_order_relaxed),
            a.totalNs.load(std::memory_order_relaxed),
            a.calls.load(std::memory_order_relaxed),
        };
    }
    void ResetStats(Atomic& a) {
        a.bytes.store(0, std::memory_order_relaxed);
        a.totalNs.store(0, std::memory_order_relaxed);
        a.calls.store(0, std::memory_order_relaxed);
    }
}

void AssetReadProfiling::Record(const Source src, const uint64_t bytes, const uint64_t ns) {
    auto& a = (src == Source::Archive) ? g_archive : g_loose;
    a.bytes.fetch_add(bytes, std::memory_order_relaxed);
    a.totalNs.fetch_add(ns, std::memory_order_relaxed);
    a.calls.fetch_add(1, std::memory_order_relaxed);
}

void AssetReadProfiling::BeginLoad() {
    ResetStats(g_archive);
    ResetStats(g_loose);
}

AssetReadProfiling::Stats AssetReadProfiling::SnapshotArchive() { return LoadStats(g_archive); }
AssetReadProfiling::Stats AssetReadProfiling::SnapshotLoose()   { return LoadStats(g_loose); }
