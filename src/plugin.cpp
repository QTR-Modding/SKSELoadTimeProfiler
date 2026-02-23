#include "Hooks.h"
#include "Logger.h"
#include "MCP.h"
#include "MessagingProfiler.h"
#include "Settings.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    MCP::loadTimeMs.store(-1.0, std::memory_order_relaxed);
    SKSE::Init(skse);
    MessagingProfiler::SetRegisterSpanStartNow();
    Hooks::Install();
    Settings::Load();
    MessagingProfiler::Install();
    logger::info("Plugin loaded");
    MCP::Register();

    return true;
}