#include "LoadProfiling.h"

#include "AssetReadProfiling.h"
#include "ChangeFormProfiling.h"
#include "Export.h"
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

    // Recursive so a post-load Export::WriteSnapshot call (from FinalizeLocked) can
    // re-enter LoadProfiling::Snapshot to assemble the save-load section of the export.
    std::recursive_mutex g_mutex;
    std::vector<LoadProfiling::LoadRecord> g_history;
    std::atomic<uint64_t> g_order{0};
    bool g_seenMainMenu{false};  // at the Main Menu and not yet entered the game (cold start pending)

    // In-progress Main Menu -> gameplay transition (guarded by g_mutex): save (kPreLoadGame),
    // new game (kNewGame), or a menu `coc` (neither). In-game cell loads fire no message.
    struct InProgress {
        bool        active{false};
        bool        sawPre{false};
        bool        sawNew{false};
        bool        coldStart{false};  // transition originated at the Main Menu
        std::string name;
        bool        success{true};
        uint64_t    tMenuOpen{0};
        uint64_t    tPre{0};
        uint64_t    tNew{0};
        uint64_t    tPost{0};
        uint64_t    tLoadEvent{0};
        uint64_t    tGlobalStart{0};
        uint64_t    tGlobalEnd{0};
        double      papyrusMs{-1.0};
    } g_cur;

    const char* KindLabel(const InProgress& c) {
        if (c.sawPre) return "Save";
        if (c.sawNew) return "New game";
        return "coc/other";
    }

    uint64_t StartAnchor(const InProgress& c) {
        if (c.sawPre) return c.tPre;
        if (c.sawNew) return c.tNew;
        return c.tMenuOpen;
    }

    LoadProfiling::LoadRecord MakeRecord(const InProgress& c, const uint64_t tMenuClose) {
        const uint64_t tStart = StartAnchor(c);
        LoadProfiling::LoadRecord rec;
        rec.name          = c.name;
        rec.kind          = KindLabel(c);
        rec.success       = c.success;
        rec.deserializeMs = DiffMs(c.tPre, c.tPost);
        rec.papyrusMs     = c.papyrusMs;
        // Deserialize sub-phases from change-form header timestamps (same steady clock).
        const uint64_t firstForm = ChangeFormProfiling::FirstFormNs();
        const uint64_t lastForm  = ChangeFormProfiling::LastFormNs();
        rec.preFormMs  = DiffMs(c.tPre, firstForm);
        rec.formSpanMs = DiffMs(firstForm, lastForm);
        rec.postFormMs = DiffMs(lastForm, c.tPost);
        rec.globalDataMs = DiffMs(c.tGlobalStart, c.tGlobalEnd);
        if (rec.postFormMs >= 0.0 && rec.globalDataMs >= 0.0)
            rec.postFormOtherMs = rec.postFormMs - rec.globalDataMs;  // tail outside global-data span
        rec.menuVisibleMs = DiffMs(c.tMenuOpen, tMenuClose);
        rec.inControlMs   = DiffMs(tStart, c.tLoadEvent);
        rec.postToCloseMs = DiffMs(c.tPost, tMenuClose);
        rec.startNs       = tStart;
        return rec;
    }

    // Caller holds g_mutex. Writes the current g_history snapshot to csv/txt/json.
    void ExportSnapshotLocked() {
        std::string status;
        const bool csvOk = Export::WriteSnapshot(Export::Format::Csv, status);
        const bool txtOk = Export::WriteSnapshot(Export::Format::Txt, status);
        const bool jsonOk = Export::WriteSnapshot(Export::Format::Json, status);
        if (csvOk && txtOk && jsonOk)
            logger::info("[LoadProfiler] post-load snapshot exported: {}", status);
        else
            logger::warn("[LoadProfiler] post-load export incomplete (csv={}, txt={}, json={})", csvOk, txtOk, jsonOk);
    }

    // Caller holds g_mutex.
    void FinalizeLocked(const uint64_t tMenuClose) {
        if (!g_cur.active) return;
        auto rec = MakeRecord(g_cur, tMenuClose);
        rec.order = g_order.fetch_add(1, std::memory_order_relaxed);
        logger::info(
            "[LoadProfiler] {} '{}' ({}): deserialize(pre->post)={:.1f}ms (papyrus={:.1f}ms), "
            "menu-visible={:.1f}ms, in-control={:.1f}ms, trailing(post->menuClose)={:.1f}ms",
            rec.kind, rec.name.empty() ? "<unknown>" : rec.name, rec.success ? "ok" : "FAILED",
            rec.deserializeMs, rec.papyrusMs, rec.menuVisibleMs, rec.inControlMs, rec.postToCloseMs);

        // Deserialize decomposition: where the load time actually goes.
        if (rec.kind == "Save") {
            logger::info("[LoadProfiler]   deserialize phases: pre-form(read+mods)={:.1f}ms, "
                         "change-forms(loops)={:.1f}ms, global-data(incl cells/refs/3D)={:.1f}ms "
                         "(papyrus={:.1f}ms), post-form-tail={:.1f}ms",
                         rec.preFormMs, rec.formSpanMs, rec.globalDataMs, rec.papyrusMs,
                         rec.postFormOtherMs);
        }

        // Top mods by change-form deserialize cost (per-mod attribution).
        if (rec.kind == "Save") {
            const auto rows = ChangeFormProfiling::SnapshotLast();
            if (!rows.empty()) {
                logger::info("[LoadProfiler]   change-forms: {} forms, {:.1f}ms total; top mods:",
                             ChangeFormProfiling::LastTotalCount(), ChangeFormProfiling::LastTotalMs());
                const std::size_t topN = std::min<std::size_t>(rows.size(), 10);
                for (std::size_t i = 0; i < topN; ++i) {
                    const auto& r = rows[i];
                    logger::info("[LoadProfiler]     {:>5} forms  {:>8.2f}ms  {}", r.count, r.totalMs, r.plugin);
                }
            }
        }

        // BSA vs raw (loose-file) asset reads observed across the load window.
        const auto bsa = AssetReadProfiling::SnapshotArchive();
        const auto loose = AssetReadProfiling::SnapshotLoose();
        if (bsa.calls || loose.calls) {
            logger::info(
                "[LoadProfiler]   asset reads: BSA={} calls, {:.1f}MB, {:.1f}ms  |  loose={} calls, {:.1f}MB, {:.1f}ms",
                bsa.calls, static_cast<double>(bsa.bytes) / (1024.0 * 1024.0),
                static_cast<double>(bsa.totalNs) / 1'000'000.0,
                loose.calls, static_cast<double>(loose.bytes) / (1024.0 * 1024.0),
                static_cast<double>(loose.totalNs) / 1'000'000.0);
        }

        // Push + clear g_cur BEFORE exporting: Export's Snapshot() must read this finalized
        // record (real tMenuClose), not the still-active in-progress one (tMenuClose=0, which
        // exports menu-visible/trailing as -1). Save-only -- other kinds carry no breakdown.
        const bool isSave = rec.kind == "Save";
        g_history.push_back(std::move(rec));
        if (g_cur.coldStart) g_seenMainMenu = false;  // we have entered the game
        g_cur = InProgress{};
        if (isSave) ExportSnapshotLocked();
    }

    // Caller holds g_mutex. Begin tracking if not already, preserving an existing menu-open.
    void EnsureActiveLocked() {
        if (g_cur.active) return;
        g_cur = InProgress{};
        g_cur.active = true;
    }

    class LoadEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                                public RE::BSTEventSink<RE::TESLoadGameEvent>,
                                public REX::Singleton<LoadEventSink> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (!event) return RE::BSEventNotifyControl::kContinue;
            const uint64_t now = NowNs();
            std::lock_guard lk(g_mutex);

            if (event->menuName == RE::MainMenu::MENU_NAME) {
                if (event->opening) g_seenMainMenu = true;
                return RE::BSEventNotifyControl::kContinue;
            }
            if (event->menuName != RE::LoadingMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;

            if (event->opening) {
                const bool cold = g_seenMainMenu;
                // A cold start (Main Menu -> gameplay) is tracked even if no load message
                // arrives (coc). Save/new-game loads may have already begun tracking.
                if (cold) EnsureActiveLocked();
                if (g_cur.active) {
                    g_cur.tMenuOpen = now;
                    g_cur.coldStart = g_cur.coldStart || cold;
                    // A coc fires no kPreLoadGame/kNewGame, so begin the accumulators here
                    // (resets stale startup reads, sets the TSC calibration anchor).
                    if (cold && !g_cur.sawPre && !g_cur.sawNew) {
                        ChangeFormProfiling::BeginLoad();
                        AssetReadProfiling::BeginLoad();
                    }
                }
            } else if (g_cur.active) {
                // Capture only real entries into the game; drop in-game cell transitions.
                if (g_cur.sawPre || g_cur.sawNew || g_cur.coldStart) {
                    FinalizeLocked(now);
                } else {
                    g_cur = InProgress{};
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*,
                                              RE::BSTEventSource<RE::TESLoadGameEvent>*) override {
            const uint64_t now = NowNs();
            std::lock_guard lk(g_mutex);
            if (g_cur.active && g_cur.tLoadEvent == 0) {
                g_cur.tLoadEvent = now;
            } else if (!g_history.empty() && g_history.back().inControlMs < 0.0 &&
                       g_history.back().kind == "Save" && g_history.back().startNs != 0) {
                // TESLoadGameEvent commonly fires just after the Loading Menu closes (the
                // record is already finalized): back-fill in-control from the start anchor.
                auto& rec = g_history.back();
                rec.inControlMs = DiffMs(rec.startNs, now);
                logger::info("[LoadProfiler] in-control (pre->TESLoadGameEvent) for '{}' = {:.1f}ms (back-filled)",
                             rec.name.empty() ? "<unknown>" : rec.name, rec.inControlMs);
                ExportSnapshotLocked();  // re-export so the file carries in-control (-1 at finalize)
            }
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
    ChangeFormProfiling::BeginLoad();    // reset per-mod attribution accumulators
    AssetReadProfiling::BeginLoad();     // reset BSA/loose accumulators
    std::lock_guard lk(g_mutex);
    if (g_cur.active) FinalizeLocked(0);  // flush a prior load that never saw its menu close
    g_cur = InProgress{};
    g_cur.active = true;
    g_cur.sawPre = true;
    g_cur.tPre = now;
    g_cur.coldStart = g_seenMainMenu;
    if (saveName && saveName[0] != '\0') g_cur.name.assign(saveName);
}

void LoadProfiling::OnNewGame() {
    const uint64_t now = NowNs();
    ChangeFormProfiling::BeginLoad();  // new game has no change-forms, but reset for cleanliness
    AssetReadProfiling::BeginLoad();   // scope asset reads to the new-game window + set TSC anchor
    std::lock_guard lk(g_mutex);
    if (g_cur.active) FinalizeLocked(0);
    g_cur = InProgress{};
    g_cur.active = true;
    g_cur.sawNew = true;
    g_cur.tNew = now;
    g_cur.coldStart = g_seenMainMenu;
}

void LoadProfiling::RecordPapyrusRestore(const double ms) {
    std::lock_guard lk(g_mutex);
    if (g_cur.active) g_cur.papyrusMs = ms;
}

void LoadProfiling::OnGlobalDataStart() {
    const uint64_t now = NowNs();
    std::lock_guard lk(g_mutex);
    if (g_cur.active && g_cur.tGlobalStart == 0) g_cur.tGlobalStart = now;  // first InitGlobalData
}

void LoadProfiling::OnGlobalDataEnd() {
    const uint64_t now = NowNs();
    std::lock_guard lk(g_mutex);
    if (g_cur.active) g_cur.tGlobalEnd = now;  // last FinishLoadGlobalData (overwrites; last wins)
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
    // Surface an in-progress entry that already has a core anchor (post or load event)
    // but whose LoadingMenu has not closed yet, so the data is visible immediately.
    if (g_cur.active && (g_cur.tPost != 0 || g_cur.tLoadEvent != 0)) {
        auto rec = MakeRecord(g_cur, 0);
        rec.order = g_order.load(std::memory_order_relaxed);
        out.push_back(std::move(rec));
    }
    return out;
}
