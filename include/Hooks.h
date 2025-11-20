#pragma once

namespace ESPProfiling {
    std::vector<std::pair<std::string, uint64_t>> SnapshotTotals();
}

namespace Hooks {
	void Install();

	class TESLoad {
    public:
        static void Install();

    private:
        static int64_t thunk(int64_t a1, RE::TESFile* file, char a2);
        static inline REL::Relocation<decltype(thunk)> originalFunction;
    };

    class OpenTESHook {
    public:
        static void Install();
    private:
        static bool thunk1(RE::TESFile* file, RE::NiFile::OpenMode a_accessMode, bool a_lock);
        static bool thunk2(RE::TESFile* file, RE::NiFile::OpenMode a_accessMode, bool a_lock);
        static inline REL::Relocation<decltype(thunk1)> originalFunction1;
        static inline REL::Relocation<decltype(thunk2)> originalFunction2;
    };

    class CloseTESHook {
    public:
        static void Install();
    private:
        static bool thunk6(RE::TESFile* file, bool a_force);
        static bool thunk7(RE::TESFile* file, bool a_force);
        static inline REL::Relocation<decltype(thunk6)> originalFunction6;
        static inline REL::Relocation<decltype(thunk7)> originalFunction7;
    };
};