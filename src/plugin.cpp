#include "MCP.h"
#include "Logger.h"
#include "MessagingProfiler.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        MessagingProfiler::Dump();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);
    MessagingProfiler::Install();
    logger::info("Plugin loaded");
    MCP::Register();

    SKSE::GetMessagingInterface()->RegisterListener(&OnMessage);
    return true;
}