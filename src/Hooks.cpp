#include "Hooks.h"
#include <unordered_map>

namespace {
    template <class Fn, class... Args>
    auto TimeCall(const std::string& nameStr, Fn&& fn, Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).
            count());
        if (name) ESPProfiling::Record(name, ns);
        return result;
    }
}


std::vector<std::pair<std::string, uint64_t>> ESPProfiling::SnapshotTotals() {
    std::lock_guard lk(g_mutex);
    std::vector<std::pair<std::string, uint64_t>> out;
    out.reserve(g_entries.size());
    for (auto& [k, v] : g_entries) out.emplace_back(k, v.totalNs);
    return out;
}

void ESPProfiling::Record(const std::string_view espName, const uint64_t ns) {
    std::lock_guard lk(g_mutex);
    std::string key(espName);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) it = g_entries.emplace(key, Entry{.name = key, .totalNs = 0, .maxNs = 0, .count = 0}).first;
    auto& e = it->second;
    e.count++;
    e.totalNs += ns;
    if (ns > e.maxNs) e.maxNs = ns;
}


void Hooks::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    SKSE::AllocTrampoline(14 * 5);
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
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    return TimeCall(filename, fn, a1, file, a2);
}

void Hooks::OpenTESHook::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction1 =
        a_trampoline.write_call<5>(
        REL::RelocationID(13645, 13753).address() + REL::Relocate(0x24b, 0x23b), thunk1);
    SKSE::AllocTrampoline(14);
    originalFunction2 =
        a_trampoline.write_call<5>(
        REL::RelocationID(13645, 13753).address() + REL::Relocate(0x2ab, 0x28b), thunk2);
}

bool Hooks::OpenTESHook::thunk1(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction1.get();
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    return TimeCall(filename, fn, file, m, l);
}

bool Hooks::OpenTESHook::thunk2(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction2.get();
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    return TimeCall(filename, fn, file, m, l);
}

void Hooks::CloseTESHook::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction6 =
        a_trampoline.write_call<5>(
        REL::RelocationID(13638, 13743).address() + REL::Relocate(0x430, 0x110), thunk6);
    SKSE::AllocTrampoline(14);
    originalFunction7 =
        a_trampoline.write_call<5>(
        REL::RelocationID(13639, 13744).address() + REL::Relocate(0x1ac, 0x1b0), thunk7);
}

bool Hooks::CloseTESHook::thunk6(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction6.get();
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    return TimeCall(filename, fn, file, a_force);
}

bool Hooks::CloseTESHook::thunk7(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction7.get();
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    return TimeCall(filename, fn, file, a_force);
}