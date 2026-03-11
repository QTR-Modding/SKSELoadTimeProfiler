#pragma once

namespace ESPProfiling {
    enum class Phase {
        Load,
        Open,
        Close
    };

    struct Entry {
        std::string name;
        std::string author;
        double version{-1.0};
        uint64_t totalNs{0};
        uint64_t maxNs{0};
        uint64_t count{0};
        uint64_t openNs{0};   // OpenTES duration: from VRESL on VR, from OpenTESHook on SE/AE (0 if not available)
        uint64_t closeNs{0};  // CloseTES duration from CloseTESHook (survives on all platforms)
        uint64_t startNs{0};  // steady_clock ns when first loaded (for trace timestamps) (only used in JSON output)
        uint64_t order{0};    // insertion order (real load sequence) (only used in JSON output)
    };

    std::vector<Entry> SnapshotEntries();
    std::vector<std::pair<std::string, uint64_t>> SnapshotTotals();

    void Record(std::string_view espName, uint64_t ns, Phase phase,
                     std::string_view author = {}, double version = -1.0);
    void RecordLoad(std::string_view espName, uint64_t ns, std::string_view author = {}, double version = -1.0);
    void RecordOpen(std::string_view espName, uint64_t ns, std::string_view author = {}, double version = -1.0);
    void RecordClose(std::string_view espName, uint64_t ns, std::string_view author = {}, double version = -1.0);

    // Overwrite an existing entry's timing with new values (does not accumulate).
    // Creates a new entry if none exists. Used by VRESL integration to replace
    // LTP's file-close times with VRESL's ConstructObjectList+OpenTES times.
    void Replace(std::string_view espName, uint64_t ns, uint64_t openNs = 0,
                 std::string_view author = {}, double version = -1.0);
    void SetCurrentLoading(std::string_view espName);
    void ClearCurrentLoading();
    std::string GetCurrentLoading();
    double GetCurrentLoadingElapsedMs();
}
