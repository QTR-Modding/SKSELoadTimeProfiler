#pragma once

#include <mutex>
#include <string>
#include <vector>

// Profiles the "enter the game" pipeline, which is distinct from the initial
// data-file load the ESP/DLL profilers measure. The core save-load span is fully
// SKSE-native (kPreLoadGame -> kPostLoadGame) and needs no RE addresses, so it is
// VR-safe and version-proof. LoadingMenu, MainMenu and TESLoadGameEvent are used to
// bound the user-perceived span and to also capture New Game / coc cold starts that
// never fire kPreLoadGame.
namespace LoadProfiling {
    struct LoadRecord {
        std::string name;            // save file name (kPreLoadGame), else label/empty
        std::string kind;            // "Save", "New game", or "coc/other"
        bool        success{true};   // from kPostLoadGame payload (save loads)
        // Durations in ms; -1 if the anchor pair was not observed.
        double deserializeMs{-1.0};  // kPreLoadGame  -> kPostLoadGame     (save only: read + forms + globals)
        double papyrusMs{-1.0};      // Papyrus/SkyrimVM script-state restore (sub-phase of deserialize)
        double menuVisibleMs{-1.0};  // LoadingMenu open -> close          (user-perceived, all kinds)
        double inControlMs{-1.0};    // start anchor  -> TESLoadGameEvent  (save: fully loaded)
        double postToCloseMs{-1.0};  // kPostLoadGame -> LoadingMenu close (save: trailing world load)
        uint64_t startNs{0};         // steady_clock ns at the start anchor (trace origin)
        uint64_t order{0};           // load sequence
    };

    // Register LoadingMenu/MainMenu (MenuOpenCloseEvent) and TESLoadGameEvent sinks.
    // Call once UI/event sources exist (kDataLoaded).
    void Install();

    // SKSE messaging anchors, driven from the plugin message listener.
    void OnPreLoadGame(const char* saveName);  // kPreLoadGame: msg->data = save name
    void OnPostLoadGame(bool success);         // kPostLoadGame: msg->data = bool success
    void OnNewGame();                          // kNewGame: starting a brand-new game

    // Record the Papyrus/SkyrimVM load-game restore duration (driven by a hook on the
    // VM restore function); attributed to the in-progress load if one is active.
    void RecordPapyrusRestore(double ms);

    std::vector<LoadRecord> Snapshot();
}
