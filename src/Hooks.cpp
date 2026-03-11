#include "Hooks.h"
#include "ESPProfiling.h"


namespace {
    std::string GetFilename(const RE::TESFile* file) {
        if (!file) return "<null>";
        const auto sv = file->GetFilename();
        return {sv.data(), sv.size()};
    }

    std::string GetCreatedBy(const RE::TESFile* file) {
        if (!file) return {};
        const char* author = file->createdBy.c_str();
        if (!author || author[0] == '\0') return {};
        return author;
    }

    double GetPluginVersion(const RE::TESFile* file) {
        if (!file) return -1.0;
        return file->version;
    }

    template <class Fn, class... Args>
    auto TimeCallLoad(const std::string& nameStr, const std::string& authorStr, const double version, Fn&& fn,
                  Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        if (name) ESPProfiling::RecordLoad(name, ns, authorStr, version);
        return result;
    }

    template <class Fn, class... Args>
    auto TimeCallOpen(const std::string& nameStr, const std::string& authorStr, const double version, Fn&& fn,
                      Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        if (name) ESPProfiling::RecordOpen(name, ns, authorStr, version);
        return result;
    }

    template <class Fn, class... Args>
    auto TimeCallClose(const std::string& nameStr, const std::string& authorStr, const double version, Fn&& fn,
                       Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        if (name) ESPProfiling::RecordClose(name, ns, authorStr, version);
        return result;
    }
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
    // VR: ConstructObjectList is called from CompileFiles (ID 13645) at +0x2c3.
    // With SkyrimVRESL installed, VRESL also patches this call site — since VRESL loads
    // after us (alphabetically), it will overwrite our hook, silently disabling ESP
    // profiling when VRESL is active. No crash in either case.
    // SE/AE: hook the call to ConstructObjectList inside LoadFileObjects/CompileFiles.
    originalFunction = a_trampoline.write_call<5>(
        REL::RelocationID(13687, 13753, 13645).address() + REL::Relocate(0x5e, 0x323, 0x2c3), thunk);
}

int64_t Hooks::TESLoad::thunk(int64_t a1, RE::TESFile* file, char a2) {
    auto fn = originalFunction.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCallLoad(filename, author, version, fn, a1, file, a2);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

void Hooks::OpenTESHook::Install(SKSE::Trampoline& a_trampoline) {
    // VR has one OpenTES call in CompileFiles (at +0x242) vs SE's two (+0x24B, +0x2AB).
    // On VR with SkyrimVRESL, +0x242 falls inside VRESL's NOP sled so the hook is
    // harmlessly dead — VRESL jumps over it. Without VRESL, it fires normally.
    originalFunction1 = a_trampoline.write_call<5>(
        REL::RelocationID(13645, 13753).address() + REL::Relocate(0x24b, 0x23b, 0x242), thunk1);
    if (REL::Module::IsVR()) return;
    originalFunction2 =
        a_trampoline.write_call<5>(REL::RelocationID(13645, 13753).address() + REL::Relocate(0x2ab, 0x28b), thunk2);
}

bool Hooks::OpenTESHook::thunk1(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction1.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCallOpen(filename, author, version, fn, file, m, l);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::OpenTESHook::thunk2(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction2.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCallOpen(filename, author, version, fn, file, m, l);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

void Hooks::CloseTESHook::Install(SKSE::Trampoline& a_trampoline) {
    originalFunction6 =
        a_trampoline.write_call<5>(REL::RelocationID(13638, 13743).address() + REL::Relocate(0x430, 0x110), thunk6);
    originalFunction7 = a_trampoline.write_call<5>(
        REL::RelocationID(13639, 13744).address() + REL::Relocate(0x1ac, 0x1b0, 0x1a2), thunk7);
}

bool Hooks::CloseTESHook::thunk6(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction6.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCallClose(filename, author, version, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::CloseTESHook::thunk7(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction7.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    auto result = TimeCallClose(filename, author, version, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}