#include "ESPProfiling.h"
#include <unordered_map>

namespace {
    std::mutex g_mutex;
    std::unordered_map<std::string, ESPProfiling::Entry> g_entries;
    std::atomic<uint64_t> g_nextOrder{0};

    std::mutex g_currentMutex;
    std::string g_currentLoading;
    long long g_currentStartNs{-1};
}

std::vector<ESPProfiling::Entry> ESPProfiling::SnapshotEntries() {
    std::lock_guard lk(g_mutex);
    std::vector<Entry> out;
    out.reserve(g_entries.size());
    for (const auto& value : g_entries | std::views::values) out.push_back(value);
    return out;
}

std::vector<std::pair<std::string, uint64_t>> ESPProfiling::SnapshotTotals() {
    std::lock_guard lk(g_mutex);
    std::vector<std::pair<std::string, uint64_t>> out;
    out.reserve(g_entries.size());
    for (auto& [k, v] : g_entries) out.emplace_back(k, v.totalNs);
    return out;
}

void ESPProfiling::Record(const std::string_view espName, const uint64_t ns, const Phase phase,
                               const std::string_view author, const double version) {
    std::lock_guard lk(g_mutex);
    std::string key(espName);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        it = g_entries.emplace(key, Entry{.name = key,
                                          .author = std::string(author),
                                          .version = version,
                                          .totalNs = 0,
                                          .maxNs = 0,
                                          .count = 0,
                                          // g_currentStartNs is valid here: SetCurrentLoading was
                                          // called before Record and ClearCurrentLoading hasn't run.
                                          // ESP loading is serial so no race with g_currentMutex.
                                          .startNs = static_cast<uint64_t>(std::max(0LL, g_currentStartNs)),
                                          .order = g_nextOrder.fetch_add(1, std::memory_order_relaxed)})
                      .first;
    }

    auto& e = it->second;
    if (e.author.empty() && !author.empty()) e.author.assign(author.data(), author.size());
    if (e.version < 0.0 && version >= 0.0) e.version = version;

    e.count++;
    e.totalNs += ns;
    if (ns > e.maxNs) e.maxNs = ns;

    switch (phase) {
        case Phase::Open:
            e.openNs += ns;
            break;
        case Phase::Close:
            e.closeNs += ns;
            break;
        case Phase::Load:
        default:
            break;
    }
}

void ESPProfiling::RecordLoad(const std::string_view espName, const uint64_t ns, const std::string_view author,
                          const double version) {
    Record(espName, ns, Phase::Load, author, version);
}

void ESPProfiling::RecordOpen(const std::string_view espName, const uint64_t ns, const std::string_view author,
                              const double version) {
    Record(espName, ns, Phase::Open, author, version);
}

void ESPProfiling::RecordClose(const std::string_view espName, const uint64_t ns, const std::string_view author,
                               const double version) {
    Record(espName, ns, Phase::Close, author, version);
}

void ESPProfiling::Replace(const std::string_view espName, const uint64_t ns, const uint64_t openNs,
                           const std::string_view author, const double version) {
    std::lock_guard lk(g_mutex);
    std::string key(espName);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        g_entries.emplace(key, Entry{.name = key,
                                     .author = std::string(author),
                                     .version = version,
                                     .totalNs = ns,
                                     .maxNs = ns,
                                     .count = 1,
                                     .openNs = openNs,
                                     .startNs = static_cast<uint64_t>(std::max(0LL, g_currentStartNs)),
                                     .order = g_nextOrder.fetch_add(1, std::memory_order_relaxed)});
        return;
    }
    auto& e = it->second;
    e.totalNs = ns;
    e.maxNs   = ns;
    e.count   = 1;
    e.openNs  = openNs;
    if (e.author.empty() && !author.empty()) e.author.assign(author.data(), author.size());
    if (e.version < 0.0 && version >= 0.0) e.version = version;
}

void ESPProfiling::SetCurrentLoading(const std::string_view espName) {
    std::lock_guard lk(g_currentMutex);
    g_currentLoading.assign(espName.data(), espName.size());
    g_currentStartNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void ESPProfiling::ClearCurrentLoading() {
    std::lock_guard lk(g_currentMutex);
    g_currentLoading.clear();
    g_currentStartNs = -1;
}

std::string ESPProfiling::GetCurrentLoading() {
    std::lock_guard lk(g_currentMutex);
    return g_currentLoading;
}

double ESPProfiling::GetCurrentLoadingElapsedMs() {
    std::lock_guard lk(g_currentMutex);
    if (g_currentLoading.empty() || g_currentStartNs < 0) return -1.0;
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
        .count();
    if (nowNs < g_currentStartNs) return -1.0;
    return static_cast<double>(nowNs - g_currentStartNs) / 1'000'000.0;
}
