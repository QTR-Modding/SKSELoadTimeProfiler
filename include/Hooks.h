#pragma once

namespace ESPProfiling {
    struct Entry {
        std::string name;
        uint64_t totalNs{0};
        uint64_t maxNs{0};
        uint64_t count{0};
    };

    inline std::mutex g_mutex;
    inline std::unordered_map<std::string, Entry> g_entries;

    std::vector<std::pair<std::string, uint64_t>> SnapshotTotals();

    void Record(std::string_view espName, uint64_t ns);
}

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
};