#pragma once

#include <mutex>
#include <string>
#include <vector>

// Profiles the "load a save -> in the game" pipeline, which is distinct from the
// initial data-file load the ESP/DLL profilers measure. The core span is fully
// SKSE-native (kPreLoadGame -> kPostLoadGame) and needs no RE addresses, so it is
// VR-safe and version-proof. LoadingMenu and TESLoadGameEvent are recorded as
// cross-checks to validate the user-perceived boundary.
namespace LoadProfiling {
    struct LoadRecord {
        std::string name;            // save file name (from kPreLoadGame)
        bool        success{true};   // from kPostLoadGame payload
        // Durations in ms; -1 if the anchor pair was not observed.
        double deserializeMs{-1.0};  // kPreLoadGame  -> kPostLoadGame     (core SKSE span)
        double menuVisibleMs{-1.0};  // LoadingMenu open -> close          (user-perceived)
        double inControlMs{-1.0};    // kPreLoadGame  -> TESLoadGameEvent  (fully loaded)
        double postToCloseMs{-1.0};  // kPostLoadGame -> LoadingMenu close (trailing world load)
        uint64_t startNs{0};         // steady_clock ns at kPreLoadGame (trace origin)
        uint64_t order{0};           // load sequence
    };

    // Register LoadingMenu (MenuOpenCloseEvent) and TESLoadGameEvent sinks.
    // Call once UI/event sources exist (kDataLoaded).
    void Install();

    // SKSE messaging anchors, driven from the plugin message listener.
    void OnPreLoadGame(const char* saveName);  // msg->data = save name
    void OnPostLoadGame(bool success);         // msg->data = bool success

    std::vector<LoadRecord> Snapshot();
}
