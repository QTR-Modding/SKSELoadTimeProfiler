#include "Hooks.h"
#include "Logger.h"
#include "MCP.h"
#include "Localization.h"
#include "MessagingProfiler.h"
#include "Events.h"
#include "Settings.h"

namespace {
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                logger::info("Received kDataLoaded message, installing events");
                Events::Install();
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