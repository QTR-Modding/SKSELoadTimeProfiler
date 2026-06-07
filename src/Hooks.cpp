#include "Hooks.h"

#include "AssetReadProfiling.h"
#include "ChangeFormProfiling.h"
#include "ESPProfiling.h"
#include "LoadProfiling.h"


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
    auto TimeCall(const std::string& nameStr, const std::string& authorStr, const double version,
                  const ESPProfiling::Phase phase, Fn&& fn, Args&&... args) {
        const char* name = nameStr.empty() ? nullptr : nameStr.c_str();
        const auto start = std::chrono::high_resolution_clock::now();
        auto result = fn(std::forward<Args>(args)...);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        if (name) ESPProfiling::Record(name, ns, phase, authorStr, version);
        return result;
    }
}

void Hooks::Install() {
    auto& trampoline = SKSE::GetTrampoline();
    constexpr size_t size_per_hook = 14;
    constexpr size_t NUM_TRAMPOLINE_HOOKS = 11;  // 5 ESP + 1 Papyrus + 3 change-form + 2 global-data
    trampoline.create(size_per_hook * NUM_TRAMPOLINE_HOOKS);
    TESLoad::Install(trampoline);
    OpenTESHook::Install(trampoline);
    CloseTESHook::Install(trampoline);
    PapyrusLoadHook::Install(trampoline);
    ChangeFormHook::Install(trampoline);
    GlobalDataHook::Install(trampoline);
    AssetReadHook::Install();
}

void Hooks::GlobalDataHook::Install(SKSE::Trampoline& a_trampoline) {
    // First InitGlobalData call = global-data start; last FinishLoadGlobalData = end.
    REL::Relocation<std::uintptr_t> loadGame{REL::RelocationID(34677, 35600)};
    const auto base = loadGame.address();
    const auto firstInit  = REL::Relocate<std::uintptr_t>(0x8fe, 0x968, 0x8f6);
    const auto lastFinish = REL::Relocate<std::uintptr_t>(0x946, 0x9b0, 0x93e);
    originalInit   = a_trampoline.write_call<5>(base + firstInit, initThunk);
    originalFinish = a_trampoline.write_call<5>(base + lastFinish, finishThunk);
    logger::debug("GlobalDataHook init@{:x} finish@{:x}", base + firstInit, base + lastFinish);
}

std::uintptr_t Hooks::GlobalDataHook::initThunk(void* a1, void* a2, void* a3, void* a4) {
    LoadProfiling::OnGlobalDataStart();  // first InitGlobalData = global-data begins
    return originalInit(a1, a2, a3, a4);
}

std::uintptr_t Hooks::GlobalDataHook::finishThunk(void* a1, void* a2, void* a3, void* a4) {
    auto result = originalFinish(a1, a2, a3, a4);
    LoadProfiling::OnGlobalDataEnd();  // last FinishLoadGlobalData = global-data done
    return result;
}

void Hooks::PapyrusLoadHook::Install(SKSE::Trampoline& a_trampoline) {
    // write_call the direct call to the VM restore inside LoadPapyrus (+0x1d); the restore
    // is vtable-dispatched so its entry can't be trampoline-detoured. See commit for RE.
    REL::Relocation<std::uintptr_t> callSite{REL::RelocationID(53207, 54018, 53207), 0x1d};
    originalFunction = a_trampoline.write_call<5>(callSite.address(), thunk);
    logger::debug("PapyrusLoadHook call site @ {:x}", callSite.address());
}

std::uintptr_t Hooks::PapyrusLoadHook::thunk(void* a_this, void* a2, void* a3, void* a4) {
    const auto start = std::chrono::high_resolution_clock::now();
    auto result = originalFunction(a_this, a2, a3, a4);
    const auto end = std::chrono::high_resolution_clock::now();
    LoadProfiling::RecordPapyrusRestore(std::chrono::duration<double, std::milli>(end - start).count());
    return result;
}

void Hooks::ChangeFormHook::Install(SKSE::Trampoline& a_trampoline) {
    // write_call the 3 direct call sites to the change-form header read in LoadGame
    // (main loop / deferred / retry); per-runtime call-site offsets verified in Ghidra.
    REL::Relocation<std::uintptr_t> loadGame{REL::RelocationID(34677, 35600)};
    const auto base = loadGame.address();
    const auto offset0 = REL::Relocate<std::uintptr_t>(0x3d0, 0x440, 0x3d0);
    const auto offset1 = REL::Relocate<std::uintptr_t>(0x71a, 0x78a, 0x712);
    const auto offset2 = REL::Relocate<std::uintptr_t>(0x770, 0x7e0, 0x768);
    originalFunction0 = a_trampoline.write_call<5>(base + offset0, thunk0);
    originalFunction1 = a_trampoline.write_call<5>(base + offset1, thunk1);
    originalFunction2 = a_trampoline.write_call<5>(base + offset2, thunk2);
    logger::debug("ChangeFormHook call sites @ {:x}, {:x}, {:x}",
                  base + offset0, base + offset1, base + offset2);
}

namespace {
    // The header read writes the FormID to *RCX (formID at +0) on return; capture entry ts
    // + FormID and let ChangeFormProfiling bill the inter-iteration delta to the prev form.
    inline void RecordOne(void* a_data, void* a_file, Hooks::ChangeFormHook::Fn* orig) {
        const uint64_t entryNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        orig(a_data, a_file);
        if (a_data) {
            ChangeFormProfiling::RecordForm(*static_cast<const uint32_t*>(a_data), entryNs);
        }
    }
}

void Hooks::ChangeFormHook::thunk0(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction0.get()); }
void Hooks::ChangeFormHook::thunk1(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction1.get()); }
void Hooks::ChangeFormHook::thunk2(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction2.get()); }

void Hooks::AssetReadHook::Install() {
    // ArchiveStream::DoRead at vtable slot 6 (CompressedArchiveStream shares this vfunc).
    {
        REL::Relocation<std::uintptr_t> vtbl{REL::RelocationID(285761, 236985)};
        originalArchive = vtbl.write_vfunc(6, archiveThunk);
        logger::debug("AssetReadHook archive vtable @ {:x}, slot 6", vtbl.address());
    }
    // LooseFileStream vtable: SE id 285903 and VR (via 285903) index the vtable start,
    // DoRead at slot 6; the AE id 332171 indexes the DoRead entry directly (slot 0).
    {
        REL::Relocation<std::uintptr_t> vtbl{REL::RelocationID(285903, 332171)};
        const size_t slot = REL::Module::IsAE() ? 0 : 6;
        originalLoose = vtbl.write_vfunc(slot, looseThunk);
        logger::debug("AssetReadHook loose vtable @ {:x}, slot {}", vtbl.address(), slot);
    }
}

uint32_t Hooks::AssetReadHook::archiveThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br) {
    // Hot path (~200K calls/load): __rdtsc instead of chrono to minimize overhead.
    const uint64_t t0 = __rdtsc();
    const uint32_t status = originalArchive(a_this, a_buf, a_count, a_br);
    const uint64_t cycles = __rdtsc() - t0;
    const uint64_t bytes = (a_br && status == 0) ? *a_br : 0;
    AssetReadProfiling::Record(AssetReadProfiling::Source::Archive, bytes, cycles);
    return status;
}

uint32_t Hooks::AssetReadHook::looseThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br) {
    const uint64_t t0 = __rdtsc();
    const uint32_t status = originalLoose(a_this, a_buf, a_count, a_br);
    const uint64_t cycles = __rdtsc() - t0;
    const uint64_t bytes = (a_br && status == 0) ? *a_br : 0;
    AssetReadProfiling::Record(AssetReadProfiling::Source::Loose, bytes, cycles);
    return status;
}

void Hooks::TESLoad::Install(SKSE::Trampoline& a_trampoline) {
    // Hook the ConstructObjectList call in CompileFiles. GOTCHA: SkyrimVRESL patches the
    // same VR call site and loads after us, overwriting this hook (ESP profiling off under
    // VRESL; no crash).
    originalFunction = a_trampoline.write_call<5>(
        REL::RelocationID(13687, 13753, 13645).address() + REL::Relocate(0x5e, 0x323, 0x2c3), thunk);
}

int64_t Hooks::TESLoad::thunk(int64_t a1, RE::TESFile* file, char a2) {
    auto fn = originalFunction.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Load, fn, a1, file, a2);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

void Hooks::OpenTESHook::Install(SKSE::Trampoline& a_trampoline) {
    // VR has one OpenTES call in CompileFiles (+0x242) vs SE's two; under VRESL +0x242 is
    // in VRESL's NOP sled (harmlessly dead), else it fires.
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
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Open, fn, file, m, l);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::OpenTESHook::thunk2(RE::TESFile* file, RE::NiFile::OpenMode m, bool l) {
    auto fn = originalFunction2.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Open, fn, file, m, l);
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
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Close, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}

bool Hooks::CloseTESHook::thunk7(RE::TESFile* file, bool a_force) {
    auto fn = originalFunction7.get();
    const auto filename = GetFilename(file);
    const auto author = GetCreatedBy(file);
    const double version = GetPluginVersion(file);
    ESPProfiling::SetCurrentLoading(filename);
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Close, fn, file, a_force);
    ESPProfiling::ClearCurrentLoading();
    return result;
}