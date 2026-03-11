#include "VRESLIntegration.h"
#include "ESPProfiling.h"
#include <SkyrimVRESLAPI.h>
#include <unordered_set>

namespace {
    // Cached at kPostPostLoad; nullptr if VRESL not present or too old.
    SkyrimVRESLPluginAPI::ISkyrimVRESLInterface002* g_interface = nullptr;
}

void VRESLIntegration::ConnectIfPresent() {
    if (!REL::Module::IsVR()) return;

    // Try interface 002 first (has plugin load timings).
    g_interface = SkyrimVRESLPluginAPI::GetSkyrimVRESLInterface002();
    if (g_interface) {
        logger::info("[VRESLIntegration] Connected to SkyrimVRESL interface 002 (build {})",
                     g_interface->GetBuildNumber());
        return;
    }

    // Interface 002 unavailable — check 001 to distinguish outdated VRESL from absent.
    if (const auto iface001 = SkyrimVRESLPluginAPI::GetSkyrimVRESLInterface001()) {
        logger::warn("[VRESLIntegration] SkyrimVRESL detected (build {}) but interface 002 is not available. "
                     "Plugin load timing and file open phase data cannot be imported — "
                     "VRESL's NOP sled kills the hooks at those call sites. "
                     "Please update SkyrimVRESL to restore full profiling functionality.",
                     iface001->GetBuildNumber());
    }
    // else: VRESL not installed — silent, all hooks run normally.
}

void VRESLIntegration::ImportIfPresent() {
    if (!g_interface) return;

    uint32_t count = 0;
    const auto data = g_interface->GetPluginLoadTimings(&count);
    logger::info("[VRESLIntegration] GetPluginLoadTimings returned {} entries", count);
    if (!data || count == 0) return;

    // On VR with VRESL, the ConstructObjectList and OpenTES hooks are killed by
    // VRESL's NOP sled, so ESPProfiling may already contain entries from surviving
    // CloseTES hooks (file-close times). VRESL's ConstructObjectList timing is more
    // representative of plugin load cost, so Replace rather than skip.
    uint32_t replaced = 0, added = 0;
    const auto existing = ESPProfiling::SnapshotEntries();
    std::unordered_set<std::string> alreadyRecorded;
    alreadyRecorded.reserve(existing.size());
    for (const auto& e : existing) alreadyRecorded.insert(e.name);

    for (uint32_t i = 0; i < count; ++i) {
        const auto& t = data[i];
        if (!t.filename || t.filename[0] == '\0') continue;
        logger::debug("[VRESLIntegration] {} totalNs={} openNs={} count={}", t.filename, t.totalNs, t.openNs, t.count);
        const bool existed = alreadyRecorded.contains(t.filename);
        ESPProfiling::Replace(t.filename, t.totalNs, t.openNs,
                              t.author ? t.author : "",
                              t.version);
        if (existed) ++replaced;
        else ++added;
    }

    logger::info("[VRESLIntegration] VRESL timing applied: {} replaced existing, {} added new (total {})",
                 replaced, added, count);
}