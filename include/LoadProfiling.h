#pragma once

#include <mutex>
#include <string>
#include <vector>

// Profiles the "enter the game" pipeline (distinct from the ESP/DLL data-file load). The core
// span is SKSE-native (kPreLoadGame -> kPostLoadGame, no RE addresses, VR-safe); LoadingMenu /
// MainMenu / TESLoadGameEvent bound the user-perceived span and catch New Game / coc cold starts.
namespace LoadProfiling {
    struct LoadRecord {
        std::string name;            // save file name (kPreLoadGame), else label/empty
        std::string kind;            // "Save", "New game", or "coc/other"
        bool        success{true};   // from kPostLoadGame payload (save loads)
        // Durations in ms; -1 if the anchor pair was not observed.
        double deserializeMs{-1.0};  // kPreLoadGame  -> kPostLoadGame     (save only: read + forms + globals)
        // Deserialize sub-phases (save only), derived from change-form header timestamps:
        double preFormMs{-1.0};      // kPreLoadGame -> first change-form  (file read + LoadMods)
        double formSpanMs{-1.0};     // first -> last change-form          (change-form loops, wall-clock)
        double postFormMs{-1.0};     // last change-form -> kPostLoadGame  (global data + cell/3D load)
        // InitGlobalData -> FinishLoadGlobalData. INCLUDES cell/reference/3D load (cells are
        // a global-data type), so typically ~all of post-form -- the dominant cost.
        double globalDataMs{-1.0};
        double postFormOtherMs{-1.0};  // post-form outside the global-data span (small tail)
        double papyrusMs{-1.0};        // Papyrus/SkyrimVM script-state restore (within global-data)
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

    // Global-data load span markers (driven by hooks on the first InitGlobalData /
    // last FinishLoadGlobalData calls inside LoadGame).
    void OnGlobalDataStart();
    void OnGlobalDataEnd();

    std::vector<LoadRecord> Snapshot();
}
