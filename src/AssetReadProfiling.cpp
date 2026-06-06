#include "AssetReadProfiling.h"

#include <atomic>
#include <intrin.h>

namespace {
    struct Atomic {
        std::atomic<uint64_t> bytes{0};
        std::atomic<uint64_t> cycles{0};
        std::atomic<uint64_t> calls{0};
    };
    Atomic g_archive;
    Atomic g_loose;

    // Calibration window anchors captured at BeginLoad; used to derive TSC frequency.
    std::atomic<uint64_t> g_qpc0{0};
    std::atomic<uint64_t> g_tsc0{0};

    uint64_t QpcNow() {
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return static_cast<uint64_t>(li.QuadPart);
    }
    uint64_t QpcFreq() {
        static const uint64_t freq = [] {
            LARGE_INTEGER li;
            QueryPerformanceFrequency(&li);
            return static_cast<uint64_t>(li.QuadPart);
        }();
        return freq;
    }

    void ResetStats(Atomic& a) {
        a.bytes.store(0, std::memory_order_relaxed);
        a.cycles.store(0, std::memory_order_relaxed);
        a.calls.store(0, std::memory_order_relaxed);
    }

    AssetReadProfiling::Stats LoadStats(const Atomic& a) {
        const uint64_t cycles = a.cycles.load(std::memory_order_relaxed);
        // Derive TSC cycles/sec from the elapsed load window: (tscNow - tsc0) over
        // (qpcNow - qpc0)/qpcFreq seconds. Robust to invariant-TSC scaling differences.
        const uint64_t qpc0 = g_qpc0.load(std::memory_order_relaxed);
        const uint64_t tsc0 = g_tsc0.load(std::memory_order_relaxed);
        uint64_t totalNs = 0;
        if (qpc0 != 0 && tsc0 != 0) {
            const uint64_t qpcElapsed = QpcNow() - qpc0;
            const uint64_t tscElapsed = __rdtsc() - tsc0;
            if (qpcElapsed > 0 && tscElapsed > 0) {
                const double seconds = static_cast<double>(qpcElapsed) / static_cast<double>(QpcFreq());
                const double cyclesPerSec = static_cast<double>(tscElapsed) / seconds;
                if (cyclesPerSec > 0.0) {
                    totalNs = static_cast<uint64_t>(static_cast<double>(cycles) / cyclesPerSec * 1e9);
                }
            }
        }
        return AssetReadProfiling::Stats{
            a.bytes.load(std::memory_order_relaxed),
            totalNs,
            a.calls.load(std::memory_order_relaxed),
        };
    }
}

void AssetReadProfiling::Record(const Source src, const uint64_t bytes, const uint64_t cycles) {
    auto& a = (src == Source::Archive) ? g_archive : g_loose;
    a.bytes.fetch_add(bytes, std::memory_order_relaxed);
    a.cycles.fetch_add(cycles, std::memory_order_relaxed);
    a.calls.fetch_add(1, std::memory_order_relaxed);
}

void AssetReadProfiling::BeginLoad() {
    ResetStats(g_archive);
    ResetStats(g_loose);
    g_qpc0.store(QpcNow(), std::memory_order_relaxed);
    g_tsc0.store(__rdtsc(), std::memory_order_relaxed);
}

AssetReadProfiling::Stats AssetReadProfiling::SnapshotArchive() { return LoadStats(g_archive); }
AssetReadProfiling::Stats AssetReadProfiling::SnapshotLoose()   { return LoadStats(g_loose); }
