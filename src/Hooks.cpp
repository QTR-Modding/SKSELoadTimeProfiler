#include "Hooks.h"
#include <unordered_map>


// Common timing wrapper
namespace {
    template <class Fn, class... Args>
    auto TimeCall(const char* tag, const std::string& nameStr, Fn&& fn, Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).
            count());
        if (name) ESPProfiling::Record(name, ns);
        logger::info("[ESP] {} {} took {:.3f} ms", tag, name ? name : "<null>", ns / 1'000'000.0);
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
    if (it == g_entries.end()) it = g_entries.emplace(key, Entry{key, 0, 0, 0}).first;
    auto& e = it->second;
    e.count++;
    e.totalNs += ns;
    if (ns > e.maxNs) e.maxNs = ns;
}


void Hooks::Install() {
    TESLoad::Install();
    OpenTESHook::Install();
    CloseTESHook::Install();
}

void Hooks::TESLoad::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    SKSE::AllocTrampoline(14);
    originalFunction = trampoline.write_call<5>(REL::RelocationID(13687, 13753).address() + REL::Relocate(0x5e, 0x323),
                                                thunk);
}

int64_t Hooks::TESLoad::thunk(int64_t a1, RE::TESFile* file, char a2) {
    auto fn = originalFunction.get();
    std::string filename;
    if (file) {
        const auto sv = file->GetFilename();
        filename.assign(sv.data(), sv.size());
    } else { filename = "<null>"; }
    logger::info("TESLoad thunk ({})", filename);
    return TimeCall("Load", filename, fn, a1, file, a2);
}

void Hooks::OpenTESHook::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    SKSE::AllocTrampoline(14);
    originalFunction1 = trampoline.write_call<5>(
        REL::RelocationID(13645, 13753).address() + REL::Relocate(0x24b, 0x23b), thunk1);
    SKSE::AllocTrampoline(14);
    originalFunction2 = trampoline.write_call<5>(
        REL::RelocationID(13645, 13753).address() + REL::Relocate(0x2ab, 0x28b), thunk2);
}

bool Hooks::OpenTESHook::thunk1(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    logger::info("Open thunk1 {}", file ? file->GetFilename() : "<null>");
    return originalFunction1(file, m, l);
}

bool Hooks::OpenTESHook::thunk2(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    logger::info("Open thunk2 {}", file ? file->GetFilename() : "<null>");
    return originalFunction2(file, m, l);
}

void Hooks::CloseTESHook::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    SKSE::AllocTrampoline(14);
    originalFunction6 = trampoline.write_call<5>(
        REL::RelocationID(13638, 13743).address() + REL::Relocate(0x430, 0x110), thunk6);
    SKSE::AllocTrampoline(14);
    originalFunction7 = trampoline.write_call<5>(
        REL::RelocationID(13639, 13744).address() + REL::Relocate(0x1ac, 0x1b0), thunk7);
}

bool Hooks::CloseTESHook::thunk6(RE::TESFile* file, bool a_force) {
    logger::info("Close thunk6 {}", file ? file->GetFilename() : "<null>");
    return originalFunction6(file, a_force);
}

bool Hooks::CloseTESHook::thunk7(RE::TESFile* file, bool a_force) {
    logger::info("Close thunk7 {}", file ? file->GetFilename() : "<null>");
    return originalFunction7(file, a_force);
}