#include "MCP.h"
#include "Logger.h"
#include "MessagingProfiler.h"
#include "Settings.h"
#include "Hooks.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);
    Hooks::Install();
    LogSettings::Load();
    MessagingProfiler::Install();
    logger::info("Plugin loaded");
    MCP::Register();

    return true;
}