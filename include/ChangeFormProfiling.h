#pragma once

#include <string>
#include <vector>

// Per-mod change-form deserialize attribution: during a save load, each form
// processed inside BGSSaveLoadGame::LoadGame's change-form loop is keyed by
// (FormID >> 24) -- the source plugin's load-order index. Accumulating per-form
// time/count by that byte and resolving to plugin names via TESDataHandler tells
// users which mods are bloating their saves.
namespace ChangeFormProfiling {
    struct Row {
        std::string plugin;      // plugin file name, "ESL FE<idx>", or "unresolved (0xNN)"
        uint64_t    count{0};    // # change-forms attributed
        double      totalMs{0.0};
    };

    // Reset per-load accumulators. Called at the start of each save load.
    void BeginLoad();

    // Hook callback, called once per change-form loop iteration with the form's decoded
    // FormID and the monotonic timestamp (ns) at the header-read entry. The FormID
    // header read is cheap; the expensive part (LookupFormById + the polymorphic
    // Revert/apply) runs BEFORE the next header read, so we attribute the wall-time
    // delta between consecutive iterations to the previous form's plugin -- giving the
    // real per-mod application cost, not just the header-decode time.
    void RecordForm(uint32_t formID, uint64_t entryNs);

    // Returns the rows for the most recently completed load, sorted by total time.
    // Plugin names are resolved lazily on first call after each load.
    std::vector<Row> SnapshotLast();

    // Sum across all plugins for the most recently completed load.
    uint64_t LastTotalCount();
    double   LastTotalMs();

    // steady_clock ns at the first / last change-form header read of the current load
    // (0 if none). Same clock as LoadProfiling, used to derive deserialize sub-phases:
    //   pre-form  = kPreLoadGame -> FirstFormNs (file read + LoadMods)
    //   forms     = FirstFormNs  -> LastFormNs  (change-form loops)
    //   post-form = LastFormNs   -> kPostLoadGame (global data + cell/3D + Papyrus)
    uint64_t FirstFormNs();
    uint64_t LastFormNs();
}
