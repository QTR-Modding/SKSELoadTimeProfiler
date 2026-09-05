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

    // Times the Papyrus/SkyrimVM load-game restore (once per load). write_call on the direct
    // call site in LoadPapyrus (+0x1d); args forwarded generically (RCX/RDX/R8/R9).
    class PapyrusLoadHook {
    public:
        static void Install(SKSE::Trampoline& a_trampoline);

    private:
        static std::uintptr_t thunk(void* a_this, void* a2, void* a3, void* a4);
        static inline REL::Relocation<decltype(thunk)> originalFunction;
    };

    // Attributes each change-form's load time to its source plugin: write_call the 3 call
    // sites to the change-form header read in LoadGame, read the FormID from [RCX] on return.
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

    // Brackets the global-data load span in LoadGame: write_call on the FIRST InitGlobalData
    // and LAST FinishLoadGlobalData calls, splitting post-form into global-data vs cell/3D.
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

    // Vtable detours on ArchiveStream::DoRead and LooseFileStream::DoRead for the BSA-vs-loose
    // breakdown (CompressedArchiveStream shares ArchiveStream's vfunc, so one hook covers both).
    class AssetReadHook {
    public:
        // DoRead vfunc: returns a 32-bit status; R9 is a uint64* WRITTEN with bytes-read on
        // return -- read it after the original.
        using DoReadFn = uint32_t(void* a_this, void* a_buffer, uint64_t a_count, uint64_t* a_bytesRead);

        static void Install();

    private:
        static uint32_t archiveThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br);
        static uint32_t looseThunk(void* a_this, void* a_buf, uint64_t a_count, uint64_t* a_br);
        static inline REL::Relocation<DoReadFn> originalArchive;
        static inline REL::Relocation<DoReadFn> originalLoose;
    };
};