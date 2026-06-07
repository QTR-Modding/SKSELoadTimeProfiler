#pragma once

#include <string>
#include <vector>

// Per-mod change-form deserialize attribution: each form in LoadGame's change-form loop is
// keyed by (FormID >> 24) = load-order index, then resolved to a plugin name -- surfacing
// which mods bloat the save.
namespace ChangeFormProfiling {
    struct Row {
        std::string plugin;      // plugin file name, "ESL FE<idx>", or "unresolved (0xNN)"
        uint64_t    count{0};    // # change-forms attributed
        double      totalMs{0.0};
    };

    // Reset per-load accumulators. Called at the start of each save load.
    void BeginLoad();

    // Hook callback, once per loop iteration (decoded FormID + entry ns). The apply cost
    // (lookup + Revert/apply) runs between header reads, so the inter-iteration delta is
    // billed to the previous form -- real per-mod cost, not just header decode.
    void RecordForm(uint32_t formID, uint64_t entryNs);

    // Returns the rows for the most recently completed load, sorted by total time.
    // Plugin names are resolved lazily on first call after each load.
    std::vector<Row> SnapshotLast();

    // Sum across all plugins for the most recently completed load.
    uint64_t LastTotalCount();
    double   LastTotalMs();

    // First / last change-form header-read ns (0 if none), same clock as LoadProfiling. Splits
    // deserialize: pre-form = pre->First, forms = First->Last, post-form = Last->post (globals).
    uint64_t FirstFormNs();
    uint64_t LastFormNs();
}
