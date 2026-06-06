#include "ChangeFormProfiling.h"

#include <array>
#include <atomic>
#include <mutex>

namespace {
    // Index 0..0xFF = top byte of FormID (load-order index). ESL plugins use 0xFE.
    // Indexed accumulators are lock-free atomic so the hot-path thunk doesn't lock.
    struct Bucket {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
    };
    std::array<Bucket, 256> g_cur;

    // Iteration-timing state (single load thread touches these). The change-form loop
    // is: read header -> lookup -> Revert/apply -> next header. We attribute the
    // wall-time between consecutive header reads to the PREVIOUS form (its apply cost).
    std::atomic<uint64_t> g_lastEntryNs{0};
    std::atomic<uint8_t>  g_lastLo{0};
    // Gaps larger than this are loop boundaries (global-data init / cell setup between
    // the three change-form loops), not a single form -- don't attribute them.
    constexpr uint64_t kIterGapCutoffNs = 100'000'000;  // 100ms

    // Snapshot of the most recently completed load, plus resolved plugin names.
    std::mutex g_lastMutex;
    std::array<std::pair<uint64_t, uint64_t>, 256> g_lastRaw{};  // count, totalNs
    std::vector<ChangeFormProfiling::Row> g_lastResolved;
    bool g_lastDirty{true};
    bool g_haveLoad{false};

    std::string ResolvePluginName(uint8_t loIdx) {
        // Resolve a load-order index to a plugin file name. 0xFF is unused / reserved
        // (often dynamic). 0xFE is the ESL flag -- file is in TESDataHandler::smallFiles.
        // We only have the top byte, not the 12-bit ESL sub-index, so all 0xFE forms
        // collapse into a single "ESL (multiple)" bucket here.
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return std::string("unresolved (0x") + (loIdx < 16 ? "0" : "") + std::to_string(loIdx) + ")";

        if (loIdx == 0xFE) return std::string("ESL (FE xxx multiple)");
        if (loIdx == 0xFF) return std::string("dynamic / runtime");

        // GetLoadedMods/Count is the cross-runtime accessor and returns full mods
        // indexed 0..0xFD by load order; ESL/0xFE files live in the small-files array.
        auto** const mods = dh->GetLoadedMods();
        const auto count = dh->GetLoadedModCount();
        if (mods && loIdx < count) {
            if (const auto* file = mods[loIdx]) {
                const auto sv = file->GetFilename();
                return std::string(sv.data(), sv.size());
            }
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "unresolved (0x%02X)", loIdx);
        return buf;
    }
}

void ChangeFormProfiling::BeginLoad() {
    for (auto& b : g_cur) {
        b.count.store(0, std::memory_order_relaxed);
        b.totalNs.store(0, std::memory_order_relaxed);
    }
    g_lastEntryNs.store(0, std::memory_order_relaxed);
    g_lastLo.store(0, std::memory_order_relaxed);
    std::lock_guard lk(g_lastMutex);
    g_lastDirty = true;  // any pending snapshot is stale
}

void ChangeFormProfiling::RecordForm(uint32_t formID, uint64_t entryNs) {
    const uint8_t lo = static_cast<uint8_t>(formID >> 24);
    g_cur[lo].count.fetch_add(1, std::memory_order_relaxed);  // count this form
    // Attribute the elapsed time since the previous form's header read to that form
    // (its full lookup + apply cost), skipping inter-loop gaps.
    const uint64_t last = g_lastEntryNs.exchange(entryNs, std::memory_order_relaxed);
    if (last != 0 && entryNs > last) {
        const uint64_t delta = entryNs - last;
        if (delta < kIterGapCutoffNs) {
            g_cur[g_lastLo.load(std::memory_order_relaxed)].totalNs.fetch_add(delta, std::memory_order_relaxed);
        }
    }
    g_lastLo.store(lo, std::memory_order_relaxed);
}

namespace {
    // Caller holds g_lastMutex. Snap the current accumulators into g_lastRaw and mark
    // the resolved cache stale.
    void SnapCurrentLocked() {
        for (uint32_t i = 0; i < 256; ++i) {
            g_lastRaw[i].first  = g_cur[i].count.load(std::memory_order_relaxed);
            g_lastRaw[i].second = g_cur[i].totalNs.load(std::memory_order_relaxed);
        }
        g_lastDirty = true;
        g_haveLoad = true;
    }
}

std::vector<ChangeFormProfiling::Row> ChangeFormProfiling::SnapshotLast() {
    std::lock_guard lk(g_lastMutex);
    SnapCurrentLocked();  // always snap latest before resolving
    if (!g_lastDirty) return g_lastResolved;

    g_lastResolved.clear();
    g_lastResolved.reserve(64);
    for (uint32_t i = 0; i < 256; ++i) {
        const auto [count, ns] = g_lastRaw[i];
        if (!count) continue;
        Row row;
        row.plugin  = ResolvePluginName(static_cast<uint8_t>(i));
        row.count   = count;
        row.totalMs = static_cast<double>(ns) / 1'000'000.0;
        g_lastResolved.push_back(std::move(row));
    }
    std::sort(g_lastResolved.begin(), g_lastResolved.end(),
              [](const Row& a, const Row& b) { return a.totalMs > b.totalMs; });
    g_lastDirty = false;
    return g_lastResolved;
}

uint64_t ChangeFormProfiling::LastTotalCount() {
    uint64_t total = 0;
    for (auto& b : g_cur) total += b.count.load(std::memory_order_relaxed);
    return total;
}

double ChangeFormProfiling::LastTotalMs() {
    uint64_t total = 0;
    for (auto& b : g_cur) total += b.totalNs.load(std::memory_order_relaxed);
    return static_cast<double>(total) / 1'000'000.0;
}
