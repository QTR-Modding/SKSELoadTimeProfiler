#pragma once

namespace Hooks {
    void Install();

    class TESLoad {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        static int64_t thunk(int64_t a1, RE::TESFile* file, char a2);
        static inline REL::Relocation<decltype(thunk)> originalFunction;
    };

    class OpenTESHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        static bool thunk1(RE::TESFile* file, RE::NiFile::OpenMode a_accessMode, bool a_lock);
        static bool thunk2(RE::TESFile* file, RE::NiFile::OpenMode a_accessMode, bool a_lock);
        static inline REL::Relocation<decltype(thunk1)> originalFunction1;
        static inline REL::Relocation<decltype(thunk2)> originalFunction2;
    };

    class CloseTESHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        static bool thunk6(RE::TESFile* file, bool a_force);
        static bool thunk7(RE::TESFile* file, bool a_force);
        static inline REL::Relocation<decltype(thunk6)> originalFunction6;
        static inline REL::Relocation<decltype(thunk7)> originalFunction7;
    };

    // Times the Papyrus/SkyrimVM load-game restore (the "Loading game..." VM state
    // restore), called once per load inside BGSSaveLoadGame::LoadGame. Hooked at the
    // direct CALL site inside LoadPapyrus (+0x1d, SE/AE/VR) via write_call. Args are
    // forwarded generically (RCX/RDX/R8/R9) since the function homes R8 (>=3 args).
    class PapyrusLoadHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        static std::uintptr_t thunk(void* a_this, void* a2, void* a3, void* a4);
        static inline REL::Relocation<decltype(thunk)> originalFunction;
    };

    // Attributes each change-form's load time to its source plugin. The change-form
    // header read (which decodes the FormID into BGSLoadFormData) has three direct
    // call sites inside BGSSaveLoadGame::LoadGame; we wrap-time each via write_call,
    // read the FormID from [RCX] post-return, and bucket by load-order byte.
    class ChangeFormHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    public:
        // Signature: void f(BGSLoadFormData* data /*RCX*/, Win32FileType* file /*RDX*/)
        // The header read writes the decoded FormID to data->formID (offset 0).
        using Fn = void(void* a_data, void* a_file);

    private:
        static void thunk0(void* a_data, void* a_file);
        static void thunk1(void* a_data, void* a_file);
        static void thunk2(void* a_data, void* a_file);
        static inline REL::Relocation<Fn> originalFunction0;
        static inline REL::Relocation<Fn> originalFunction1;
        static inline REL::Relocation<Fn> originalFunction2;
    };

    // Brackets the global-data load span inside BGSSaveLoadGame::LoadGame: write_call on
    // the FIRST InitGlobalData call and the LAST FinishLoadGlobalData call. Combined with
    // the change-form timestamps, this splits post-form into global-data vs cell/3D.
    // Offsets verified SE/AE/VR (VR resolved via the SE<->VR Version-Tracking mapping --
    // the calls live in LoadGame on all three; VR's were just missing from its ref DB).
    class GlobalDataHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        using Fn = std::uintptr_t(void* a1, void* a2, void* a3, void* a4);
        static std::uintptr_t initThunk(void* a1, void* a2, void* a3, void* a4);
        static std::uintptr_t finishThunk(void* a1, void* a2, void* a3, void* a4);
        static inline REL::Relocation<Fn> originalInit;
        static inline REL::Relocation<Fn> originalFinish;
    };

    // Vtable detours on BSResource::ArchiveStream::DoRead and ::LooseFileStream::DoRead.
    // CompressedArchiveStream shares ArchiveStream's read vfunc, so a single Archive
    // hook covers all BSA reads (compressed and uncompressed). Tracks bytes + time per
    // source, so users can see the BSA-vs-loose breakdown for a save load.
    class AssetReadHook {
    public:
        // DoRead vfunc: returns a 32-bit status enum; args via Microsoft x64 calling
        // convention. R9 holds a uint64* that's WRITTEN with actual-bytes-read on
        // return -- read it after invoking the original.
        using DoReadFn = uint32_t(void* a_this, void* a_buffer, uint64_t a_count, uint64_t* a_bytesRead);

        static void Install();

    private:
        static uint32_t archiveThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br);
        static uint32_t looseThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br);
        static inline REL::Relocation<DoReadFn> originalArchive;
        static inline REL::Relocation<DoReadFn> originalLoose;
    };
};