#include "MessagingProfiler.h"
#include "Hooks.h"
#include "MCP.h"

namespace {
    std::mutex g_currentMutex;
    std::string g_currentModule;
    long long g_currentStartNs{-1};
    std::atomic<long long> g_firstRegisterNs{-1};
    std::atomic<long long> g_lastRegisterNs{-1};
    std::atomic g_regSpanFrozen{false};

    void UpdateRegisterSpan() {
        const auto first = g_firstRegisterNs.load(std::memory_order_relaxed);
        const auto last = g_lastRegisterNs.load(std::memory_order_relaxed);
        if (first < 0 || last < first) return;
        const auto diffMs = static_cast<double>(last - first) / 1'000'000.0;
        MCP::loadTimeMs.store(diffMs, std::memory_order_relaxed);
    }

    long long NowSteadyNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void UpdateMax(std::atomic<uint64_t>& target, const uint64_t value) {
        auto prev = target.load(std::memory_order_relaxed);
        while (value > prev && !target.compare_exchange_weak(prev, value, std::memory_order_relaxed)) {
        }
    }
}

std::vector<MessagingProfiler::TaggedRow> MessagingProfiler::GetTaggedRows() {
    std::vector<TaggedRow> out;
    for (auto names = GetModuleRowsSnapshot(); auto& r : names) {
        TaggedRow tr;
        tr.module = r.module;
        tr.kind = r.kind;
        tr.totalMs = (r.kind == SourceKind::ESP)
                         ? r.espTotalMs
                         : std::accumulate(r.avgMs.begin(), r.avgMs.end(), 0.0);
        tr.perMsg = r.avgMs;
        out.emplace_back(std::move(tr));
    }
    return out;
}

void MessagingProfiler::Install() {
    InitTrampolinePool();
    const auto mi = SKSE::GetMessagingInterface();
    if (!mi) {
        logger::warn("[Profiler] Messaging interface unavailable");
        return;
    }
    mi->RegisterListener("SKSE", [](SKSE::MessagingInterface::Message* msg) {
        if (msg && msg->type == SKSE::MessagingInterface::kPostLoad) {
            g_regSpanFrozen.store(true, std::memory_order_relaxed);
        }
    });
    const auto rawPtr = reinterpret_cast<const MessagingExpose*>(mi)->RawProxy();
    g_rawMessaging = const_cast<SKSE::detail::SKSEMessagingInterface*>(
        static_cast<const SKSE::detail::SKSEMessagingInterface*>(rawPtr));
    if (!g_rawMessaging) {
        logger::warn("[Profiler] Raw messaging proxy null");
        return;
    }
    g_origRegister = g_rawMessaging->RegisterListener;
    if (!g_origRegister) {
        logger::warn("[Profiler] Original RegisterListener missing");
        return;
    }
    g_rawMessaging->RegisterListener = &Hook_RegisterListener;
    logger::info("[Profiler] Messaging hooks installed");
}


const char* MessagingProfiler::MessageTypeName(const std::uint32_t t) {
    using MI = SKSE::MessagingInterface;
    switch (t) {
        case MI::kPostLoad:
            return "PostLoad";
        case MI::kPostPostLoad:
            return "PostPostLoad";
        case MI::kPreLoadGame:
            return "PreLoadGame";
        case MI::kPostLoadGame:
            return "PostLoadGame";
        case MI::kSaveGame:
            return "SaveGame";
        case MI::kDeleteGame:
            return "DeleteGame";
        case MI::kInputLoaded:
            return "InputLoaded";
        case MI::kNewGame:
            return "NewGame";
        case MI::kDataLoaded:
            return "DataLoaded";
        default:
            return "Unknown";
    }
}

std::string MessagingProfiler::ModuleNameFromAddress(const void* addr) {
    if (!addr) return "<addr-null>";
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
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

void MessagingProfiler::WrapperThunk(CallbackEntry* entry, SKSE::MessagingInterface::Message* msg) {
    {
        std::lock_guard lk(g_currentMutex);
        g_currentModule = entry->pluginName;
        g_currentStartNs = NowSteadyNs();
    }
    const auto start = std::chrono::high_resolution_clock::now();
    entry->original(msg);
    const auto end = std::chrono::high_resolution_clock::now();
    {
        std::lock_guard lk(g_currentMutex);
        g_currentModule.clear();
        g_currentStartNs = -1;
    }
    const auto ns64 =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    entry->count.fetch_add(1, std::memory_order_relaxed);
    entry->totalNs.fetch_add(ns64, std::memory_order_relaxed);
    UpdateMax(entry->maxNs, ns64);
    if (msg && msg->type < SKSE::MessagingInterface::kTotal) {
        auto& ms = entry->perMessage[msg->type];
        ms.count.fetch_add(1, std::memory_order_relaxed);
        ms.totalNs.fetch_add(ns64, std::memory_order_relaxed);
        UpdateMax(ms.maxNs, ns64);
    }
}

void MessagingProfiler::InitTrampolinePool() {
    if (g_trampBase) return;
    constexpr std::size_t totalSize = MAX_WRAPPERS * TRAMP_SIZE;
    g_trampBase =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, totalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_trampBase)
        logger::error("[Profiler] Failed to allocate trampoline pool of {} bytes", totalSize);
    else
        logger::info("[Profiler] Trampoline pool allocated: {} entries ({} KB)", MAX_WRAPPERS, totalSize / 1024);
}

MessagingProfiler::RawCallback MessagingProfiler::MakeTrampoline(std::size_t idx, CallbackEntry* entry) {
    if (!g_trampBase) {
        logger::warn("[Profiler] Trampoline pool nullptr; skipping instrumentation");
        return nullptr;
    }
    if (idx >= MAX_WRAPPERS) {
        logger::warn("[Profiler] Trampoline capacity exceeded (idx={} >= {}); skipping instrumentation", idx,
                     MAX_WRAPPERS);
        return nullptr;
    }
    uint8_t* code = g_trampBase + idx * TRAMP_SIZE;
    code[0] = 0x48;
    code[1] = 0x8B;
    code[2] = 0xD1; // mov rdx, rcx
    code[3] = 0x48;
    code[4] = 0xB9;
    *reinterpret_cast<void**>(&code[5]) = entry; // mov rcx, imm64
    code[13] = 0x48;
    code[14] = 0xB8;
    *reinterpret_cast<void**>(&code[15]) = reinterpret_cast<void*>(&WrapperThunk); // mov rax, imm64
    code[23] = 0xFF;
    code[24] = 0xE0; // jmp rax
    for (int i = 25; i < static_cast<int>(TRAMP_SIZE); ++i) code[i] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), code, TRAMP_SIZE);
    return reinterpret_cast<RawCallback>(code);
}

MessagingProfiler::RawCallback MessagingProfiler::AllocateWrapper(const RawCallback original,
                                                                  // ReSharper disable once CppParameterMayBeConstPtrOrRef
                                                                  const std::string_view sender, void* callSiteRet) {
    auto idx = g_nextIndex.load(std::memory_order_relaxed);
    while (true) {
        if (idx >= MAX_WRAPPERS) {
            logger::warn("[Profiler] Trampoline capacity exceeded (idx={} >= {}); skipping instrumentation", idx,
                         MAX_WRAPPERS);
            return original;
        }
        if (g_nextIndex.compare_exchange_weak(idx, idx + 1, std::memory_order_relaxed)) {
            break;
        }
    }
    auto& e = g_entries[idx];
    e.original = original;
    e.sender = std::string(sender);
    e.pluginName = ModuleNameFromAddress(callSiteRet);
    const auto tramp = MakeTrampoline(idx, &e);
    if (!tramp) return original;
    #ifndef NDEBUG
    logger::info("[Profiler] Instrumented module='{}' idx={} orig={} tramp={}", e.pluginName, idx,
                 fmt::ptr(original), fmt::ptr(tramp));
    #endif
    return tramp;
}

bool MessagingProfiler::Hook_RegisterListener(const SKSE::PluginHandle handle, const char* sender, void* callback) {
    if (!callback) return g_origRegister(handle, sender, callback);
    if (!sender || std::strcmp(sender, "SKSE") != 0) return g_origRegister(handle, sender, callback);

    // SKSEPluginLoad heuristic
    if (!g_regSpanFrozen.load(std::memory_order_relaxed)) {
        const auto nowNs = NowSteadyNs();
        auto expected = -1LL;
        g_firstRegisterNs.compare_exchange_strong(expected, nowNs, std::memory_order_relaxed);
        g_lastRegisterNs.store(nowNs, std::memory_order_relaxed);
        UpdateRegisterSpan();
    }

    const auto cb = reinterpret_cast<RawCallback>(callback);
    void* retAddr = _ReturnAddress();
    const auto wrapped = AllocateWrapper(cb, sender, retAddr);
    return g_origRegister(handle, sender, reinterpret_cast<void*>(wrapped));
}

std::vector<MessagingProfiler::ModuleRow> MessagingProfiler::GetModuleRowsSnapshot() {
    struct Accum {
        std::array<uint64_t, SKSE::MessagingInterface::kTotal> sumNs{};
        std::array<uint64_t, SKSE::MessagingInterface::kTotal> sumCnt{};
    };
    std::unordered_map<std::string, Accum> acc;
    const auto limit = std::min<std::size_t>(g_nextIndex.load(std::memory_order_relaxed), MAX_WRAPPERS);
    // accumulate totals per module per message type
    for (std::size_t i = 0; i < limit; ++i) {
        auto& e = g_entries[i];
        auto& a = acc[e.pluginName];
        for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
            auto& ms = e.perMessage[t];
            const uint64_t cnt = ms.count.load(std::memory_order_relaxed);
            if (!cnt) continue;
            a.sumCnt[t] += cnt;
            a.sumNs[t] += ms.totalNs.load(std::memory_order_relaxed);
        }
    }
    // Convert to rows (DLL kind)
    std::vector<ModuleRow> out;
    out.reserve(acc.size());
    for (auto& [module, a] : acc) {
        ModuleRow row;
        row.module = module;
        row.kind = SourceKind::DLL;
        for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) {
            row.avgMs[t] = a.sumCnt[t] ? (static_cast<double>(a.sumNs[t]) / a.sumCnt[t]) / 1'000'000.0 : 0.0;
        }
        out.emplace_back(std::move(row));
    }
    // Append ESP rows (no per-message breakdown, only total; keep array zero)
    for (const auto& p : ESPProfiling::SnapshotTotals()) {
        ModuleRow esp;
        esp.module = p.first;
        esp.kind = SourceKind::ESP;
        esp.espTotalMs = static_cast<double>(p.second) / 1'000'000.0;
        out.emplace_back(std::move(esp));
    }
    std::ranges::sort(out, [](const ModuleRow& A, const ModuleRow& B) { return A.module < B.module; });
    return out;
}

std::string MessagingProfiler::GetCurrentCallbackModule() {
    std::lock_guard lk(g_currentMutex);
    return g_currentModule;
}

double MessagingProfiler::GetCurrentCallbackElapsedMs() {
    std::lock_guard lk(g_currentMutex);
    if (g_currentModule.empty() || g_currentStartNs < 0) return -1.0;
    const auto nowNs = NowSteadyNs();
    if (nowNs < g_currentStartNs) return -1.0;
    return static_cast<double>(nowNs - g_currentStartNs) / 1'000'000.0;
}

void MessagingProfiler::SetRegisterSpanStartNow() {
    const auto nowNs = NowSteadyNs();
    auto expected = g_firstRegisterNs.load(std::memory_order_relaxed);
    while (expected < 0 && !g_firstRegisterNs.compare_exchange_weak(expected, nowNs, std::memory_order_relaxed)) {
    }
}

std::vector<std::string_view> MessagingProfiler::GetMessageTypeNames() {
    std::vector<std::string_view> names;
    names.reserve(SKSE::MessagingInterface::kTotal);
    for (std::uint32_t t = 0; t < SKSE::MessagingInterface::kTotal; ++t) names.emplace_back(MessageTypeName(t));
    return names;
}