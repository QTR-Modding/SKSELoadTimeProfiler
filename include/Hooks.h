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
};