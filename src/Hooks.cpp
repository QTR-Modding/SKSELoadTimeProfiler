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
    constexpr size_t NUM_TRAMPOLINE_HOOKS = 9;  // 5 ESP + 1 Papyrus + 3 change-form
    trampoline.create(size_per_hook * NUM_TRAMPOLINE_HOOKS);
    TESLoad::Install(trampoline);
    OpenTESHook::Install(trampoline);
    CloseTESHook::Install(trampoline);
    PapyrusLoadHook::Install(trampoline);
    ChangeFormHook::Install(trampoline);
    AssetReadHook::Install();
}

void Hooks::PapyrusLoadHook::Install(SKSE::Trampoline& a_trampoline) {
    // Wrap the CALL to the SkyrimVM load-game restore inside SkyrimVM::LoadPapyrus.
    // The VM restore is reached by vtable dispatch (no single entry to detour with the
    // SKSE trampoline), but LoadPapyrus calls it directly; the call site sits at +0x1d
    // on SE/AE/VR (disasm-verified). write_call replaces that direct call so we can time
    // the restore by bracketing the original (returned by write_call).
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
    // Three direct CALL sites in BGSSaveLoadGame::LoadGame target the change-form
    // header read: main change-form loop (+0x3d0 SE / +0x440 AE), deferred-changes
    // path (+0x71a SE / +0x78a AE), and post-error retry (+0x770 SE / +0x7e0 AE).
    // No VR support yet -- VR ids for LoadGame's call sites aren't in the cross-db.
    if (REL::Module::IsVR()) {
        logger::info("ChangeFormHook: VR not supported (no resolved VR ids)");
        return;
    }
    REL::Relocation<std::uintptr_t> loadGame{REL::RelocationID(34677, 35600)};
    const auto base = loadGame.address();
    const auto offset0 = REL::Relocate(static_cast<std::uintptr_t>(0x3d0),
                                       static_cast<std::uintptr_t>(0x440));
    const auto offset1 = REL::Relocate(static_cast<std::uintptr_t>(0x71a),
                                       static_cast<std::uintptr_t>(0x78a));
    const auto offset2 = REL::Relocate(static_cast<std::uintptr_t>(0x770),
                                       static_cast<std::uintptr_t>(0x7e0));
    originalFunction0 = a_trampoline.write_call<5>(base + offset0, thunk0);
    originalFunction1 = a_trampoline.write_call<5>(base + offset1, thunk1);
    originalFunction2 = a_trampoline.write_call<5>(base + offset2, thunk2);
    logger::debug("ChangeFormHook call sites @ {:x}, {:x}, {:x}",
                  base + offset0, base + offset1, base + offset2);
}

namespace {
    // Wrap-time the per-form header read and attribute by load-order byte. The header
    // read populates *RCX (BGSLoadFormData::formID is at offset 0) on return; this
    // thunk reads it after the original returns. Hot path: keep minimal.
    inline void RecordOne(void* a_data, void* a_file, Hooks::ChangeFormHook::Fn* orig) {
        const auto start = std::chrono::high_resolution_clock::now();
        orig(a_data, a_file);
        const auto end = std::chrono::high_resolution_clock::now();
        if (a_data) {
            const uint32_t formID = *static_cast<const uint32_t*>(a_data);
            const uint64_t ns =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
            ChangeFormProfiling::RecordForm(formID, ns);
        }
    }
}

void Hooks::ChangeFormHook::thunk0(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction0.get()); }
void Hooks::ChangeFormHook::thunk1(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction1.get()); }
void Hooks::ChangeFormHook::thunk2(void* a_data, void* a_file) { RecordOne(a_data, a_file, originalFunction2.get()); }

void Hooks::AssetReadHook::Install() {
    if (REL::Module::IsVR()) {
        logger::info("AssetReadHook: VR not supported (no resolved VR ids)");
        return;
    }
    // ArchiveStream vtable: SE id 285761 / AE id 236985, DoRead at slot 6 (both).
    // The AE Address Library indexes by vtable START for this one, like SE.
    {
        REL::Relocation<std::uintptr_t> vtbl{REL::RelocationID(285761, 236985)};
        originalArchive = vtbl.write_vfunc(6, archiveThunk);
        logger::debug("AssetReadHook archive vtable @ {:x}, slot 6", vtbl.address());
    }
    // LooseFileStream vtable: SE id 285903 indexes the vtable start (DoRead at slot 6);
    // AE id 332171 indexes the DoRead entry directly (slot 0). Same Relocation type, but
    // the slot index differs per runtime.
    {
        REL::Relocation<std::uintptr_t> vtbl{REL::RelocationID(285903, 332171)};
        const size_t slot = REL::Module::IsAE() ? 0 : 6;
        originalLoose = vtbl.write_vfunc(slot, looseThunk);
        logger::debug("AssetReadHook loose vtable @ {:x}, slot {}", vtbl.address(), slot);
    }
}

uint32_t Hooks::AssetReadHook::archiveThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br) {
    const auto start = std::chrono::high_resolution_clock::now();
    const uint32_t status = originalArchive(a_this, a_buf, a_count, a_br);
    const auto end = std::chrono::high_resolution_clock::now();
    const uint64_t bytes = (a_br && status == 0) ? *a_br : 0;
    AssetReadProfiling::Record(AssetReadProfiling::Source::Archive, bytes,
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    return status;
}

uint32_t Hooks::AssetReadHook::looseThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br) {
    const auto start = std::chrono::high_resolution_clock::now();
    const uint32_t status = originalLoose(a_this, a_buf, a_count, a_br);
    const auto end = std::chrono::high_resolution_clock::now();
    const uint64_t bytes = (a_br && status == 0) ? *a_br : 0;
    AssetReadProfiling::Record(AssetReadProfiling::Source::Loose, bytes,
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    return status;
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
    const auto result = TimeCall(filename, author, version, ESPProfiling::Phase::Load, fn, a1, file, a2);
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