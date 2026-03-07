#include "Hooks.h"
#include <unordered_map>

namespace {
    std::string GetFilename(RE::TESFile* file) {
        if (!file) return "<null>";
        const auto sv = file->GetFilename();
        return {sv.data(), sv.size()};
    }

    std::string GetCreatedBy(RE::TESFile* file) {
        if (!file) return {};
        const char* author = file->createdBy.c_str();
        if (!author || author[0] == '\0') return {};
        return author;
    }

    double GetPluginVersion(RE::TESFile* file) {
        if (!file) return -1.0;
        return file->version;
    }

    template <class Fn, class... Args>
    auto TimeCall(const std::string& nameStr, const std::string& authorStr, const double version, Fn&& fn,
                  Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).
            count());
        if (name) ESPProfiling::Record(name, ns, authorStr, version);
        return result;
    }
}

std::vector<ESPProfiling::Entry> ESPProfiling::SnapshotEntries() {
    std::lock_guard lk(g_mutex);
    std::vector<Entry> out;
    out.reserve(g_entries.size());
    for (const auto& [_, value] : g_entries) out.push_back(value);
    return out;
}

std::vector<std::pair<std::string, uint64_t>> ESPProfiling::SnapshotTotals() {
    std::lock_guard lk(g_mutex);
    std::vector<std::pair<std::string, uint64_t>> out;
    out.reserve(g_entries.size());
    for (auto& [k, v] : g_entries) out.emplace_back(k, v.totalNs);
    return out;
}

void ESPProfiling::Record(const std::string_view espName, const uint64_t ns, const std::string_view author,
                          const double version) {
    std::lock_guard lk(g_mutex);
    std::string key(espName);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        it = g_entries.emplace(key, Entry{.name = key,
                                          .author = std::string(author),
                                          .version = version,
                                          .totalNs = 0,
                                          .maxNs = 0,
                                          .count = 0})
                      .first;
    }

    auto& e = it->second;
    if (e.author.empty() && !author.empty()) e.author.assign(author.data(), author.size());
    if (e.version < 0.0 && version >= 0.0) e.version = version;
    e.count++;
    e.totalNs += ns;
    if (ns > e.maxNs) e.maxNs = ns;
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

void Hooks::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    constexpr size_t size_per_hook = 14;
    constexpr size_t NUM_TRAMPOLINE_HOOKS = 5;
    trampoline.create(size_per_hook * NUM_TRAMPOLINE_HOOKS);
    TESLoad::Install(trampoline);
    OpenTESHook::Install(trampoline);
    CloseTESHook::Install(trampoline);
}

void Hooks::TESLoad::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction =
        a_trampoline.write_call<5>(REL::RelocationID(13687, 13753).address() + REL::Relocate(0x5e, 0x323),
                                   thunk);
}

int64_t Hooks::TESLoad::thunk(int64_t a1, RE::TESFile* file, char a2) {
    auto fn = originalFunction.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCall(filename, author, version, fn, a1, file, a2);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

void Hooks::OpenTESHook::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction1 =
        a_trampoline.write_call<5>(
            REL::RelocationID(13645, 13753).address() + REL::Relocate(0x24b, 0x23b), thunk1);
    originalFunction2 =
        a_trampoline.write_call<5>(
            REL::RelocationID(13645, 13753).address() + REL::Relocate(0x2ab, 0x28b), thunk2);
}

bool Hooks::OpenTESHook::thunk1(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction1.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCall(filename, author, version, fn, file, m, l);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::OpenTESHook::thunk2(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction2.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCall(filename, author, version, fn, file, m, l);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

void Hooks::CloseTESHook::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction6 =
        a_trampoline.write_call<5>(
            REL::RelocationID(13638, 13743).address() + REL::Relocate(0x430, 0x110), thunk6);
    originalFunction7 =
        a_trampoline.write_call<5>(
            REL::RelocationID(13639, 13744).address() + REL::Relocate(0x1ac, 0x1b0), thunk7);
}

bool Hooks::CloseTESHook::thunk6(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction6.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCall(filename, author, version, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::CloseTESHook::thunk7(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction7.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCall(filename, author, version, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}