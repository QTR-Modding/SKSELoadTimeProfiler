#pragma once
#include <cstdint>

// Interface between SkyrimVRESL and LoadTimeProfiler for ESP timing data.
//
// SkyrimVRESL exposes timing via ISkyrimVRESLInterface002, obtained via the
// standard SKSE plugin messaging API (kMessage_GetInterface dispatch).
//
// Usage (follow the VRESL API contract):
//   kPostPostLoad → VRESLIntegration::ConnectIfPresent()
//                   Dispatches to "SkyrimVRESL" and caches the interface pointer.
//                   Must be kPostPostLoad (not kPostLoad): VRESL registers its
//                   listener during kPostLoad, so dispatching at kPostLoad races
//                   with VRESL's registration if LTP loads before VRESL.
//   kDataLoaded   → VRESLIntegration::ImportIfPresent()
//                   Calls GetPluginLoadTimings() and merges into ESPProfiling.
//                   Safe to call only after ConnectIfPresent().

namespace VRESLIntegration {
    // Obtain and cache the VRESL interface 002 pointer. Call at kPostPostLoad.
    void ConnectIfPresent();

    // Pull plugin load timings from VRESL and merge into ESPProfiling.
    // Call at kDataLoaded (after ConnectIfPresent).
    void ImportIfPresent();
}
