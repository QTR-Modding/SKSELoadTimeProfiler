#pragma once

namespace MessagingProfiler {
    using RawRegisterFn = bool(*)(SKSE::PluginHandle, const char*, void*);
    using RawDispatchFn = bool(*)(SKSE::PluginHandle, std::uint32_t, void*, std::uint32_t, const char*);
    using RawCallback = void(*)(SKSE::MessagingInterface::Message*);

    struct MsgStat {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
    };

    struct CallbackEntry {
        RawCallback original{};
        std::string sender;
        std::string pluginName;
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
        std::array<MsgStat, SKSE::MessagingInterface::kTotal> perMessage{};
    };

    void Install();
    void Dump();

    // Backend for UI
    const char* MessageTypeName(std::uint32_t t);
    std::vector<std::string_view> GetMessageTypeNames();
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations();
    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs();
}