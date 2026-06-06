#include "LoadProfiling.h"

#include "REX/REX/Singleton.h"

#include <atomic>
#include <chrono>

namespace {
    using SteadyClock = std::chrono::steady_clock;

    uint64_t NowNs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now().time_since_epoch()).count());
    }

    double DiffMs(const uint64_t from, const uint64_t to) {
        if (from == 0 || to == 0 || to < from) return -1.0;
        return static_cast<double>(to - from) / 1'000'000.0;
    }

    std::mutex g_mutex;
    std::vector<LoadProfiling::LoadRecord> g_history;
    std::atomic<uint64_t> g_order{0};
    uint64_t g_lastMenuOpenNs{0};  // most recent LoadingMenu open (a load may follow)

    // The in-progress save load, guarded by g_mutex. A save load is identified by a
    // kPreLoadGame arriving while the LoadingMenu is up; an ordinary door/fast-travel
    // cell load opens the LoadingMenu but never fires kPre/kPostLoadGame.
    struct InProgress {
        bool        active{false};
        std::string name;
        bool        success{true};
        uint64_t    tMenuOpen{0};
        uint64_t    tPre{0};
        uint64_t    tPost{0};
        uint64_t    tLoadEvent{0};
    } g_cur;

    // Builds a record from whatever anchors are present (caller holds g_mutex).
    LoadProfiling::LoadRecord MakeRecord(const InProgress& c, const uint64_t tMenuClose) {
        LoadProfiling::LoadRecord rec;
        rec.name          = c.name;
        rec.success       = c.success;
        rec.deserializeMs = DiffMs(c.tPre, c.tPost);
        rec.menuVisibleMs = DiffMs(c.tMenuOpen, tMenuClose);
        rec.inControlMs   = DiffMs(c.tPre, c.tLoadEvent);
        rec.postToCloseMs = DiffMs(c.tPost, tMenuClose);
        rec.startNs       = c.tPre ? c.tPre : c.tMenuOpen;
        return rec;
    }

    void FinalizeLocked(const uint64_t tMenuClose) {
        if (!g_cur.active) return;
        auto rec = MakeRecord(g_cur, tMenuClose);
        rec.order = g_order.fetch_add(1, std::memory_order_relaxed);
        logger::info(
            "[LoadProfiler] Save '{}' loaded ({}): deserialize(pre->post)={:.1f}ms, "
            "menu-visible={:.1f}ms, in-control(pre->LoadGameEvent)={:.1f}ms, trailing(post->menuClose)={:.1f}ms",
            rec.name.empty() ? "<unknown>" : rec.name, rec.success ? "ok" : "FAILED",
            rec.deserializeMs, rec.menuVisibleMs, rec.inControlMs, rec.postToCloseMs);
        g_history.push_back(std::move(rec));
        g_cur = InProgress{};
    }

    class LoadEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                                public RE::BSTEventSink<RE::TESLoadGameEvent>,
                                public REX::Singleton<LoadEventSink> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (!event || event->menuName != RE::LoadingMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;
            const uint64_t now = NowNs();
            std::lock_guard lk(g_mutex);
            if (event->opening) {
                g_lastMenuOpenNs = now;
            } else if (g_cur.active) {
                // LoadingMenu closing on a save load: world is ready, finalize.
                FinalizeLocked(now);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*,
                                              RE::BSTEventSource<RE::TESLoadGameEvent>*) override {
            const uint64_t now = NowNs();
            std::lock_guard lk(g_mutex);
            if (g_cur.active && g_cur.tLoadEvent == 0) g_cur.tLoadEvent = now;
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void LoadProfiling::Install() {
    auto* sink = LoadEventSink::GetSingleton();
    if (const auto ui = RE::UI::GetSingleton()) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(sink);
    } else {
        logger::error("[LoadProfiler] UI singleton unavailable; LoadingMenu timing disabled");
    }
    if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESLoadGameEvent>(sink);
    } else {
        logger::error("[LoadProfiler] ScriptEventSourceHolder unavailable; TESLoadGameEvent timing disabled");
    }
    logger::info("[LoadProfiler] Save-load profiling enabled");
}

void LoadProfiling::OnPreLoadGame(const char* saveName) {
    const uint64_t now = NowNs();
    std::lock_guard lk(g_mutex);
    // Safety: if a prior load never saw its menu-close, flush it before starting a new one.
    if (g_cur.active) FinalizeLocked(0);
    g_cur = InProgress{};
    g_cur.active = true;
    g_cur.tPre = now;
    g_cur.tMenuOpen = g_lastMenuOpenNs;  // the LoadingMenu that opened just before this load
    if (saveName && saveName[0] != '\0') g_cur.name.assign(saveName);
}

void LoadProfiling::OnPostLoadGame(const bool success) {
    const uint64_t now = NowNs();
    std::lock_guard lk(g_mutex);
    if (!g_cur.active) return;  // kPostLoadGame without a matching kPreLoadGame
    g_cur.tPost = now;
    g_cur.success = success;
}

std::vector<LoadProfiling::LoadRecord> LoadProfiling::Snapshot() {
    std::lock_guard lk(g_mutex);
    std::vector<LoadRecord> out = g_history;
    // Surface an in-progress load that already has its core span (post observed) but
    // whose LoadingMenu has not closed yet, so the data is visible immediately.
    if (g_cur.active && g_cur.tPost != 0) {
        auto rec = MakeRecord(g_cur, 0);
        rec.order = g_order.load(std::memory_order_relaxed);
        out.push_back(std::move(rec));
    }
    return out;
}
