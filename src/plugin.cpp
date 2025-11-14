#include "Hooks.h"
#include "MCP.h"
#include "Logger.h"

namespace {
    // Expose protected GetProxy()
    struct MessagingExpose : SKSE::MessagingInterface {
        const void* RawProxy() const { return GetProxy(); }
    };

    using RawRegisterFn = bool(*)(SKSE::PluginHandle, const char*, void*);
    using RawDispatchFn = bool(*)(SKSE::PluginHandle, std::uint32_t, void*, std::uint32_t, const char*);
    using RawCallback   = void(*)(SKSE::MessagingInterface::Message*);

    struct MsgStat {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
    };

    struct CallbackEntry {
        RawCallback           original{};
        std::string           sender;
        std::string           pluginName;          // resolved via caller module
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> totalNs{0};
        std::atomic<uint64_t> maxNs{0};
        std::array<MsgStat, SKSE::MessagingInterface::kTotal> perMessage{}; // per message type stats
    };

    constexpr std::size_t MAX_WRAPPERS = 64;
    std::array<CallbackEntry, MAX_WRAPPERS> g_entries;
    std::atomic<std::size_t> g_nextIndex{0};
    std::mutex g_dumpMutex;

    SKSE::detail::SKSEMessagingInterface* g_rawMessaging = nullptr;
    RawRegisterFn  g_origRegister = nullptr;
    RawDispatchFn  g_origDispatch = nullptr;

    // --- Module name resolution ------------------------------------------------
    std::string ModuleNameFromAddress(void* addr)
    {
        if (!addr) return "<addr-null>";
        HMODULE hMod = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCSTR>(addr), &hMod)) {
            return "<module-unknown>";
        }
        char pathBuf[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameA(hMod, pathBuf, sizeof(pathBuf));
        if (len == 0) {
            return "<module-name-fail>";
        }
        // Strip directory
        const std::string full(pathBuf, len);
        const auto pos = full.find_last_of("/\\");
        std::string file = (pos == std::string::npos) ? full : full.substr(pos + 1);
        // Strip extension
        const auto dot = file.find_last_of('.');
        if (dot != std::string::npos) {
            file = file.substr(0, dot);
        }
        return file;
    }

    // Wrapper generator
    #define MAKE_WRAPPER(ID) \
        extern "C" __declspec(noinline) void Wrapper_##ID(SKSE::MessagingInterface::Message* msg) { \
            auto& e = g_entries[ID]; \
            const auto start = std::chrono::high_resolution_clock::now(); \
            e.original(msg); \
            const auto end = std::chrono::high_resolution_clock::now(); \
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count(); \
            e.count.fetch_add(1, std::memory_order_relaxed); \
            e.totalNs.fetch_add(static_cast<uint64_t>(ns), std::memory_order_relaxed); \
            uint64_t prevMax = e.maxNs.load(std::memory_order_relaxed); \
            while (static_cast<uint64_t>(ns) > prevMax && \
                   !e.maxNs.compare_exchange_weak(prevMax, static_cast<uint64_t>(ns), std::memory_order_relaxed)); \
            if (msg) { \
                auto type = msg->type; \
                if (type < SKSE::MessagingInterface::kTotal) { \
                    auto& ms = e.perMessage[type]; \
                    ms.count.fetch_add(1, std::memory_order_relaxed); \
                    ms.totalNs.fetch_add(static_cast<uint64_t>(ns), std::memory_order_relaxed); \
                    uint64_t mPrev = ms.maxNs.load(std::memory_order_relaxed); \
                    while (static_cast<uint64_t>(ns) > mPrev && \
                           !ms.maxNs.compare_exchange_weak(mPrev, static_cast<uint64_t>(ns), std::memory_order_relaxed)); \
                } \
            } \
        }

    MAKE_WRAPPER(0)  MAKE_WRAPPER(1)  MAKE_WRAPPER(2)  MAKE_WRAPPER(3)  MAKE_WRAPPER(4)  MAKE_WRAPPER(5)  MAKE_WRAPPER(6)  MAKE_WRAPPER(7)
    MAKE_WRAPPER(8)  MAKE_WRAPPER(9)  MAKE_WRAPPER(10) MAKE_WRAPPER(11) MAKE_WRAPPER(12) MAKE_WRAPPER(13) MAKE_WRAPPER(14) MAKE_WRAPPER(15)
    MAKE_WRAPPER(16) MAKE_WRAPPER(17) MAKE_WRAPPER(18) MAKE_WRAPPER(19) MAKE_WRAPPER(20) MAKE_WRAPPER(21) MAKE_WRAPPER(22) MAKE_WRAPPER(23)
    MAKE_WRAPPER(24) MAKE_WRAPPER(25) MAKE_WRAPPER(26) MAKE_WRAPPER(27) MAKE_WRAPPER(28) MAKE_WRAPPER(29) MAKE_WRAPPER(30) MAKE_WRAPPER(31)
    MAKE_WRAPPER(32) MAKE_WRAPPER(33) MAKE_WRAPPER(34) MAKE_WRAPPER(35) MAKE_WRAPPER(36) MAKE_WRAPPER(37) MAKE_WRAPPER(38) MAKE_WRAPPER(39)
    MAKE_WRAPPER(40) MAKE_WRAPPER(41) MAKE_WRAPPER(42) MAKE_WRAPPER(43) MAKE_WRAPPER(44) MAKE_WRAPPER(45) MAKE_WRAPPER(46) MAKE_WRAPPER(47)
    MAKE_WRAPPER(48) MAKE_WRAPPER(49) MAKE_WRAPPER(50) MAKE_WRAPPER(51) MAKE_WRAPPER(52) MAKE_WRAPPER(53) MAKE_WRAPPER(54) MAKE_WRAPPER(55)
    MAKE_WRAPPER(56) MAKE_WRAPPER(57) MAKE_WRAPPER(58) MAKE_WRAPPER(59) MAKE_WRAPPER(60) MAKE_WRAPPER(61) MAKE_WRAPPER(62) MAKE_WRAPPER(63)

    static RawCallback g_wrapperPtrs[MAX_WRAPPERS] = {
        &Wrapper_0,&Wrapper_1,&Wrapper_2,&Wrapper_3,&Wrapper_4,&Wrapper_5,&Wrapper_6,&Wrapper_7,
        &Wrapper_8,&Wrapper_9,&Wrapper_10,&Wrapper_11,&Wrapper_12,&Wrapper_13,&Wrapper_14,&Wrapper_15,
        &Wrapper_16,&Wrapper_17,&Wrapper_18,&Wrapper_19,&Wrapper_20,&Wrapper_21,&Wrapper_22,&Wrapper_23,
        &Wrapper_24,&Wrapper_25,&Wrapper_26,&Wrapper_27,&Wrapper_28,&Wrapper_29,&Wrapper_30,&Wrapper_31,
        &Wrapper_32,&Wrapper_33,&Wrapper_34,&Wrapper_35,&Wrapper_36,&Wrapper_37,&Wrapper_38,&Wrapper_39,
        &Wrapper_40,&Wrapper_41,&Wrapper_42,&Wrapper_43,&Wrapper_44,&Wrapper_45,&Wrapper_46,&Wrapper_47,
        &Wrapper_48,&Wrapper_49,&Wrapper_50,&Wrapper_51,&Wrapper_52,&Wrapper_53,&Wrapper_54,&Wrapper_55,
        &Wrapper_56,&Wrapper_57,&Wrapper_58,&Wrapper_59,&Wrapper_60,&Wrapper_61,&Wrapper_62,&Wrapper_63
    };

    RawCallback AllocateWrapper(RawCallback original, std::string_view sender, void* callSiteRet)
    {
        const auto idx = g_nextIndex.fetch_add(1);
        if (idx >= MAX_WRAPPERS) {
            logger::warn("[Profiler] Wrapper capacity exceeded; skipping instrumentation");
            return original;
        }
        auto& e = g_entries[idx];
        e.original   = original;
        e.sender     = std::string(sender);
        e.pluginName = ModuleNameFromAddress(callSiteRet);
        return g_wrapperPtrs[idx];
    }

    // Hook: use _ReturnAddress() to identify calling module
    bool Hook_RegisterListener(SKSE::PluginHandle handle, const char* sender, void* callback)
    {
        if (!callback) {
            return g_origRegister(handle, sender, callback);
        }
        const auto cb = reinterpret_cast<RawCallback>(callback);
        const char* effSender = sender ? sender : "<global>";
        void* retAddr = _ReturnAddress(); // intrinsic
        const auto wrapped = AllocateWrapper(cb, effSender, retAddr);
        logger::info("[Profiler] RegisterListener(handle={}, sender='{}', cb={}, wrapped={}, module='{}')",
                     handle, effSender, fmt::ptr(cb), fmt::ptr(wrapped),
                     g_entries[g_nextIndex.load(std::memory_order_relaxed) - 1].pluginName);
        return g_origRegister(handle, sender, reinterpret_cast<void*>(wrapped));
    }

    bool Hook_Dispatch(SKSE::PluginHandle handle, std::uint32_t type, void* data,
                       std::uint32_t len, const char* receiver)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        const auto r = g_origDispatch(handle, type, data, len, receiver);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        if (ns > 500'000) {
            logger::info("[Profiler] Dispatch(handle={}, type={}, receiver='{}', took {:.3f} ms)",
                         handle, type, receiver ? receiver : "<broadcast>", ns / 1'000'000.0);
        }
        return r;
    }

    void InstallMessagingHook()
    {
        auto* mi = SKSE::GetMessagingInterface();
        if (!mi) {
            logger::warn("[Profiler] Messaging interface unavailable");
            return;
        }
        auto* rawPtr = reinterpret_cast<const MessagingExpose*>(mi)->RawProxy();
        g_rawMessaging = const_cast<SKSE::detail::SKSEMessagingInterface*>(
            static_cast<const SKSE::detail::SKSEMessagingInterface*>(rawPtr));
        if (!g_rawMessaging) {
            logger::warn("[Profiler] Raw messaging proxy null");
            return;
        }
        g_origRegister = g_rawMessaging->RegisterListener;
        g_origDispatch = g_rawMessaging->Dispatch;
        if (!g_origRegister) {
            logger::warn("[Profiler] Original RegisterListener missing");
            return;
        }
        g_rawMessaging->RegisterListener = &Hook_RegisterListener;
        if (g_origDispatch) {
            g_rawMessaging->Dispatch = &Hook_Dispatch;
        }
        logger::info("[Profiler] Messaging hooks installed");
    }

    void DumpMessageProfile()
    {
        std::lock_guard lk(g_dumpMutex);
        for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
            auto& e = g_entries[i];
            uint64_t c     = e.count.load(std::memory_order_relaxed);
            const uint64_t total = e.totalNs.load(std::memory_order_relaxed);
            const uint64_t maxv  = e.maxNs.load(std::memory_order_relaxed);
            double avgMs   = c ? (static_cast<double>(total) / c) / 1'000'000.0 : 0.0;
            double maxMs   = static_cast<double>(maxv) / 1'000'000.0;
            double totalMs = static_cast<double>(total) / 1'000'000.0;
            logger::info("[Profiler] sender='{}' module='{}' original={} count={} avg(ms)={:.6f} max(ms)={:.6f} total(ms)={:.6f}",
                         e.sender, e.pluginName, fmt::ptr(e.original), c, avgMs, maxMs, totalMs);
        }
    }

    // Exposed accessor for MCP UI -------------------------------------------------
    const char* MessageTypeName(std::uint32_t t) {
        using MI = SKSE::MessagingInterface;
        switch (t) {
            case MI::kPostLoad: return "PostLoad";        
            case MI::kPostPostLoad: return "PostPostLoad"; 
            case MI::kPreLoadGame: return "PreLoadGame";   
            case MI::kPostLoadGame: return "PostLoadGame"; 
            case MI::kSaveGame: return "SaveGame";         
            case MI::kDeleteGame: return "DeleteGame";     
            case MI::kInputLoaded: return "InputLoaded";   
            case MI::kNewGame: return "NewGame";           
            case MI::kDataLoaded: return "DataLoaded";     
            default: return "Unknown";                    
        }
    }

    struct ModuleRow {
        std::string module;
        std::array<double, SKSE::MessagingInterface::kTotal> avgMs{}; // average per type
    };

    std::vector<ModuleRow> GetModuleRowsSnapshot() {
        std::unordered_map<std::string, ModuleRow> map; // aggregate by module name
        for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
            auto& e = g_entries[i];
            auto& row = map[e.pluginName];
            row.module = e.pluginName;
            for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
                auto& ms = e.perMessage[t];
                const uint64_t cnt = ms.count.load(std::memory_order_relaxed);
                const uint64_t tot = ms.totalNs.load(std::memory_order_relaxed);
                if (cnt) {
                    row.avgMs[t] += (static_cast<double>(tot) / cnt) / 1'000'000.0; // accumulate averages; later normalize by number of callbacks contributing
                }
            }
        }
        // Normalize: divide by number of contributing callbacks per message type
        for (auto& row : map | std::views::values) {
            std::array<int, SKSE::MessagingInterface::kTotal> contrib{};
            // second pass to count contributors
            for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
                auto& e = g_entries[i];
                if (e.pluginName != row.module) continue;
                for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
                    if (e.perMessage[t].count.load(std::memory_order_relaxed)) {
                        contrib[t]++;
                    }
                }
            }
            for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
                if (contrib[t] > 1) {
                    row.avgMs[t] /= contrib[t];
                }
            }
        }
        std::vector<ModuleRow> out;
        out.reserve(map.size());
        for (auto& kv : map) out.push_back(std::move(kv.second));
        std::ranges::sort(out, [](const ModuleRow& a, const ModuleRow& b){ return a.module < b.module; });
        return out;
    }
}

// Public API for MCP
namespace MessagingProfilerBackend {
    std::vector<std::string_view> GetMessageTypeNames() {
        std::vector<std::string_view> names;
        names.reserve(SKSE::MessagingInterface::kTotal);
        for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
            names.emplace_back(MessageTypeName(t));
        }
        return names;
    }
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations() {
        std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> rows;
        for (auto& [module, avgMs] : GetModuleRowsSnapshot()) {
            rows.emplace_back(module, avgMs);
        }
        return rows;
    }
    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs() {
        std::array<double, SKSE::MessagingInterface::kTotal> result{};
        for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
            uint64_t totNs = 0; uint64_t cnt = 0;
            for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
                auto& e = g_entries[i];
                totNs += e.perMessage[t].totalNs.load(std::memory_order_relaxed);
                cnt   += e.perMessage[t].count.load(std::memory_order_relaxed);
            }
            result[t] = cnt ? (static_cast<double>(totNs) / cnt) / 1'000'000.0 : 0.0;
        }
        return result;
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        DumpMessageProfile();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);
    InstallMessagingHook();
    logger::info("Plugin loaded");
    Hooks::Install();
    MCP::Register();

    SKSE::GetMessagingInterface()->RegisterListener(&OnMessage);
    return true;
}