#include "MessagingProfiler.h"
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <windows.h>

namespace MessagingProfiler {
    struct MessagingExpose : SKSE::MessagingInterface { const void* RawProxy() const { return GetProxy(); } };

    // Dynamic trampoline pool
    constexpr std::size_t MAX_WRAPPERS = 4096;
    constexpr std::size_t TRAMP_SIZE   = 32;
    static    uint8_t*    g_trampBase  = nullptr;

    std::array<CallbackEntry, MAX_WRAPPERS> g_entries;
    std::atomic<std::size_t> g_nextIndex{0};
    std::mutex g_dumpMutex;

    SKSE::detail::SKSEMessagingInterface* g_rawMessaging = nullptr;
    RawRegisterFn  g_origRegister = nullptr;
    RawDispatchFn  g_origDispatch = nullptr;

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
        const std::string full(pathBuf, len);
        const auto pos = full.find_last_of("/\\");
        std::string file = (pos == std::string::npos) ? full : full.substr(pos + 1);
        const auto dot = file.find_last_of('.');
        if (dot != std::string::npos) file = file.substr(0, dot);
        return file;
    }

    void __fastcall WrapperThunk(CallbackEntry* entry, SKSE::MessagingInterface::Message* msg)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        entry->original(msg);
        const auto end   = std::chrono::high_resolution_clock::now();
        const auto ns64  = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        entry->count.fetch_add(1, std::memory_order_relaxed);
        entry->totalNs.fetch_add(ns64, std::memory_order_relaxed);
        auto prevMax = entry->maxNs.load(std::memory_order_relaxed);
        while (ns64 > prevMax && !entry->maxNs.compare_exchange_weak(prevMax, ns64, std::memory_order_relaxed)) {}
        if (msg && msg->type < SKSE::MessagingInterface::kTotal) {
            auto& ms = entry->perMessage[msg->type];
            ms.count.fetch_add(1, std::memory_order_relaxed);
            ms.totalNs.fetch_add(ns64, std::memory_order_relaxed);
            auto mPrev = ms.maxNs.load(std::memory_order_relaxed);
            while (ns64 > mPrev && !ms.maxNs.compare_exchange_weak(mPrev, ns64, std::memory_order_relaxed)) {}
        }
    }

    void InitTrampolinePool()
    {
        if (g_trampBase) return;
        const std::size_t totalSize = MAX_WRAPPERS * TRAMP_SIZE;
        g_trampBase = static_cast<uint8_t*>(::VirtualAlloc(nullptr, totalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_trampBase) logger::error("[Profiler] Failed to allocate trampoline pool of {} bytes", totalSize);
        else logger::info("[Profiler] Trampoline pool allocated: {} entries ({} KB)", MAX_WRAPPERS, totalSize/1024);
    }

    using RawCallback = MessagingProfiler::RawCallback;

    RawCallback MakeTrampoline(std::size_t idx, CallbackEntry* entry)
    {
        if (!g_trampBase) { logger::warn("[Profiler] Trampoline pool nullptr; skipping instrumentation"); return nullptr; }
        if (idx >= MAX_WRAPPERS) { logger::warn("[Profiler] Trampoline capacity exceeded at idx={}; skipping instrumentation", idx); return nullptr; }
        uint8_t* code = g_trampBase + idx * TRAMP_SIZE;
        code[0] = 0x48; code[1] = 0x8B; code[2] = 0xD1;                  // mov rdx, rcx
        code[3] = 0x48; code[4] = 0xB9; *reinterpret_cast<void**>(&code[5]) = entry; // mov rcx, imm64
        code[13]= 0x48; code[14]= 0xB8; *reinterpret_cast<void**>(&code[15])= reinterpret_cast<void*>(&WrapperThunk); // mov rax, imm64
        code[23]= 0xFF; code[24]= 0xE0;                                  // jmp rax
        for (int i = 25; i < static_cast<int>(TRAMP_SIZE); ++i) code[i] = 0x90;
        ::FlushInstructionCache(GetCurrentProcess(), code, TRAMP_SIZE);
        return reinterpret_cast<RawCallback>(code);
    }

    RawCallback AllocateWrapper(const RawCallback original, const std::string_view sender, void* callSiteRet)
    {
        const auto idx = g_nextIndex.fetch_add(1, std::memory_order_relaxed);
        if (idx >= MAX_WRAPPERS) { logger::warn("[Profiler] Trampoline capacity exceeded (idx={} >= {}); skipping instrumentation", idx, MAX_WRAPPERS); return original; }
        auto& e = g_entries[idx]; e.original = original; e.sender = std::string(sender); e.pluginName = ModuleNameFromAddress(callSiteRet);
        const auto tramp = MakeTrampoline(idx, &e);
        if (!tramp) return original;
        logger::info("[Profiler] Instrumented module='{}' idx={} orig={} tramp={}", e.pluginName, idx, fmt::ptr(original), fmt::ptr(tramp));
        return tramp;
    }

    bool Hook_RegisterListener(const SKSE::PluginHandle handle, const char* sender, void* callback)
    {
        if (!callback) return g_origRegister(handle, sender, callback);
        if (!sender || std::strcmp(sender, "SKSE") != 0) return g_origRegister(handle, sender, callback);
        const auto cb = reinterpret_cast<RawCallback>(callback);
        void* retAddr = _ReturnAddress();
        const auto wrapped = AllocateWrapper(cb, sender, retAddr);
        return g_origRegister(handle, sender, reinterpret_cast<void*>(wrapped));
    }

    bool Hook_Dispatch(SKSE::PluginHandle handle, std::uint32_t type, void* data, const std::uint32_t len, const char* receiver)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        const auto r = g_origDispatch(handle, type, data, len, receiver);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        if (ns > 500'000) logger::info("[Profiler] Dispatch(handle={}, type={}, receiver='{}', took {:.3f} ms)", handle, type, receiver ? receiver : "<broadcast>", ns / 1'000'000.0);
        return r;
    }

    void Install()
    {
        InitTrampolinePool();
        auto* mi = SKSE::GetMessagingInterface();
        if (!mi) { logger::warn("[Profiler] Messaging interface unavailable"); return; }
        auto* rawPtr = reinterpret_cast<const MessagingExpose*>(mi)->RawProxy();
        g_rawMessaging = const_cast<SKSE::detail::SKSEMessagingInterface*>(static_cast<const SKSE::detail::SKSEMessagingInterface*>(rawPtr));
        if (!g_rawMessaging) { logger::warn("[Profiler] Raw messaging proxy null"); return; }
        g_origRegister = g_rawMessaging->RegisterListener;
        g_origDispatch = g_rawMessaging->Dispatch;
        if (!g_origRegister) { logger::warn("[Profiler] Original RegisterListener missing"); return; }
        g_rawMessaging->RegisterListener = &Hook_RegisterListener;
        if (g_origDispatch) g_rawMessaging->Dispatch = &Hook_Dispatch;
        logger::info("[Profiler] Messaging hooks installed");
    }

    void Dump()
    {
        std::lock_guard lk(g_dumpMutex);
        for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
            auto& e = g_entries[i]; const uint64_t c = e.count.load(std::memory_order_relaxed); const uint64_t total = e.totalNs.load(std::memory_order_relaxed); const uint64_t maxv = e.maxNs.load(std::memory_order_relaxed);
            const double avgMs = c ? (static_cast<double>(total) / c) / 1'000'000.0 : 0.0; const double maxMs = static_cast<double>(maxv) / 1'000'000.0; const double totalMs = static_cast<double>(total) / 1'000'000.0;
            logger::info("[Profiler] module='{}' original={} count={} avg(ms)={:.6f} max(ms)={:.6f} total(ms)={:.6f}", e.pluginName, fmt::ptr(e.original), c, avgMs, maxMs, totalMs);
        }
    }

    const char* MessageTypeName(const std::uint32_t t)
    {
        using MI = SKSE::MessagingInterface;
        switch (t) {
            case MI::kPostLoad: return "PostLoad"; case MI::kPostPostLoad: return "PostPostLoad"; case MI::kPreLoadGame: return "PreLoadGame"; case MI::kPostLoadGame: return "PostLoadGame"; case MI::kSaveGame: return "SaveGame"; case MI::kDeleteGame: return "DeleteGame"; case MI::kInputLoaded: return "InputLoaded"; case MI::kNewGame: return "NewGame"; case MI::kDataLoaded: return "DataLoaded"; default: return "Unknown";
        }
    }

    struct ModuleRow { std::string module; std::array<double, SKSE::MessagingInterface::kTotal> avgMs{}; };

    std::vector<ModuleRow> GetModuleRowsSnapshot() {
        struct Accum { std::array<uint64_t, SKSE::MessagingInterface::kTotal> sumNs{}; std::array<uint64_t, SKSE::MessagingInterface::kTotal> sumCnt{}; };
        std::unordered_map<std::string, Accum> acc;
        // accumulate totals per module per message type
        for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) {
            auto& e = g_entries[i]; auto& a = acc[e.pluginName];
            for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
                auto& ms = e.perMessage[t];
                const uint64_t cnt = ms.count.load(std::memory_order_relaxed);
                if (!cnt) continue;
                a.sumCnt[t] += cnt;
                a.sumNs[t]  += ms.totalNs.load(std::memory_order_relaxed);
            }
        }
        std::vector<ModuleRow> out; out.reserve(acc.size());
        for (auto& [module, a] : acc) {
            ModuleRow row; row.module = module;
            for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
                row.avgMs[t] = a.sumCnt[t] ? (static_cast<double>(a.sumNs[t]) / a.sumCnt[t]) / 1'000'000.0 : 0.0;
            }
            out.emplace_back(std::move(row));
        }
        std::sort(out.begin(), out.end(), [](const ModuleRow& A, const ModuleRow& B){ return A.module < B.module; });
        return out;
    }

    std::vector<std::string_view> GetMessageTypeNames() { std::vector<std::string_view> names; names.reserve(SKSE::MessagingInterface::kTotal); for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) names.emplace_back(MessageTypeName(t)); return names; }

    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations() { std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> rows; for (auto& r : GetModuleRowsSnapshot()) rows.emplace_back(r.module, r.avgMs); return rows; }

    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs() { std::array<double, SKSE::MessagingInterface::kTotal> result{}; for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) { uint64_t totNs=0, cnt=0; for (std::size_t i = 0; i < g_nextIndex.load(std::memory_order_relaxed); ++i) { auto& e = g_entries[i]; totNs += e.perMessage[t].totalNs.load(std::memory_order_relaxed); cnt += e.perMessage[t].count.load(std::memory_order_relaxed);} result[t] = cnt ? (static_cast<double>(totNs) / cnt) / 1'000'000.0 : 0.0; } return result; }
}

// Adapter namespace to maintain existing UI linkage
namespace MessagingProfilerBackend {
    std::vector<std::string_view> GetMessageTypeNames() {
        return MessagingProfiler::GetMessageTypeNames();
    }
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations() {
        return MessagingProfiler::GetAverageDurations();
    }
    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs() {
        return MessagingProfiler::GetTotalsAvgMs();
    }
}
