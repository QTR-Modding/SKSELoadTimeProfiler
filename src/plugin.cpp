#include "Hooks.h"
#include "Logger.h"
#include "MCP.h"
#include "Localization.h"
#include "MessagingProfiler.h"
#include "Events.h"
#include "Settings.h"
#include "VRESLIntegration.h"

namespace {
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        switch (msg->type) {
            case SKSE::MessagingInterface::kPostPostLoad:
                // kPostPostLoad fires after ALL plugins have processed kPostLoad,
                // so VRESL has definitely registered its listener by now.
                // Dispatching at kPostLoad is too early if LTP loads before VRESL.
                VRESLIntegration::ConnectIfPresent();
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                logger::info("Received kDataLoaded message, installing events");
                Events::Install();
                // Pull VRESL timing data now that ConstructObjectListThunk has run.
                VRESLIntegration::ImportIfPresent();
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    MessagingProfiler::SetRegisterSpanStartNow();
    SetupLog();
    SKSE::Init(skse);
    MessagingProfiler::Install();
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        SKSE::stl::report_and_fail("Failed to register message listener");
        // ReSharper disable once CppUnreachableCode
        return false;
    }
    Hooks::Install();
    Localization::Load();
    Settings::Load();
    MCP::Register();
    logger::info("Plugin loaded");
    return true;
}