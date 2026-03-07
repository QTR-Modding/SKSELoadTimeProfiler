#pragma once

namespace MessagingProfiler {
    using RawRegisterFn = bool(*)(SKSE::PluginHandle, const char*, void*);
    using RawCallback = void(*)(SKSE::MessagingInterface::Message*);

    struct MsgStat {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
    };

    struct MessagingExpose : SKSE::MessagingInterface {
        const void* RawProxy() const { return GetProxy(); }
    };

    // Dynamic trampoline pool
    constexpr std::size_t MAX_WRAPPERS = 4096;
    constexpr std::size_t TRAMP_SIZE = 32;
    static uint8_t* g_trampBase = nullptr;

    enum class SourceKind { DLL, ESP };

    struct ModuleRow {
        std::string module;
        std::array<double, SKSE::MessagingInterface::kTotal> avgMs{};
        SourceKind kind = SourceKind::DLL;
        double espTotalMs = 0.0;
    };

    struct TaggedRow {
        std::string module;
        SourceKind kind;
        double totalMs;
        std::array<double, SKSE::MessagingInterface::kTotal> perMsg{};
    };

    std::vector<TaggedRow> GetTaggedRows();

    struct CallbackEntry {
        RawCallback original{};
        std::string sender;
        std::string pluginName;
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
        std::array<MsgStat, SKSE::MessagingInterface::kTotal> perMessage{};
    };

    inline std::array<CallbackEntry, MAX_WRAPPERS> g_entries;
    inline std::atomic<std::size_t> g_nextIndex{0};

    inline SKSE::detail::SKSEMessagingInterface* g_rawMessaging = nullptr;
    inline RawRegisterFn g_origRegister = nullptr;
    void Install();

    // Backend for UI
    const char* MessageTypeName(std::uint32_t t);

    std::string ModuleNameFromAddress(const void* addr);

    void __fastcall WrapperThunk(CallbackEntry* entry, SKSE::MessagingInterface::Message* msg);

    void InitTrampolinePool();

    RawCallback MakeTrampoline(std::size_t idx, CallbackEntry* entry);

    RawCallback AllocateWrapper(RawCallback original, std::string_view sender, void* callSiteRet);

    bool Hook_RegisterListener(SKSE::PluginHandle handle, const char* sender, void* callback);


    std::vector<ModuleRow> GetModuleRowsSnapshot();

    std::string GetCurrentCallbackModule();
    double GetCurrentCallbackElapsedMs();
    void SetRegisterSpanStartNow();
    std::vector<std::string_view> GetMessageTypeNames();
}