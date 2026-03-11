#include "Events.h"
#include "ESPProfiling.h"
#include "Export.h"
#include "MCP.h"
#include "MessagingProfiler.h"
#include "REX/REX/Singleton.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

class MainMenuOpenExportSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                                     public REX::Singleton<MainMenuOpenExportSink> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

    [[nodiscard]] bool IsUninstalled() const noexcept { return uninstalled; }

private:
    bool uninstalled = false;
    RE::BSEventNotifyControl UnInstall();
};

RE::BSEventNotifyControl MainMenuOpenExportSink::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (uninstalled || !event || !event->opening) return RE::BSEventNotifyControl::kContinue;
    if (event->menuName != RE::MainMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;

    if (SKSEMenuFramework::IsInstalled() && !MCP::autoExportWithMenuFramework) {
        logger::info("[Export] Main Menu auto-export skipped (SKSEMenuFramework installed and 'export' is false)");
        return UnInstall();
    }

    const auto currentDll = MessagingProfiler::GetCurrentCallbackModule();
    const auto currentEsp = ESPProfiling::GetCurrentLoading();
    if (!currentDll.empty() || !currentEsp.empty()) {
        logger::critical(
            "[Export] Main Menu opened but loading still in progress (dll='{}', esp='{}'); auto-export failed!",
            currentDll,
            currentEsp);
        return UnInstall();
    }

    std::string status;
    const bool csvOk = Export::WriteSnapshot(Export::Format::Csv, status);
    const bool txtOk = Export::WriteSnapshot(Export::Format::Txt, status);
    const bool jsonOk = Export::WriteSnapshot(Export::Format::Json, status);
    if (csvOk && txtOk && jsonOk)
        logger::info("[Export] Auto-generated CSV{}/TXT{}/JSON{} snapshots on Main Menu open: {}",
                     csvOk ? "" : " failed", txtOk ? "" : " failed", jsonOk ? "" : " failed", status);
    else
        logger::warn("[Export] Auto-export incomplete on Main Menu open (csv={}, txt={}, json={})", csvOk, txtOk,
                     jsonOk);

    return UnInstall();
}

RE::BSEventNotifyControl MainMenuOpenExportSink::UnInstall() {
    if (uninstalled) return RE::BSEventNotifyControl::kContinue;
    if (const auto ui = RE::UI::GetSingleton()) {
        ui->RemoveEventSink(this);
    }
    uninstalled = true;
    return RE::BSEventNotifyControl::kContinue;
}

void Events::Install() {
    if (MainMenuOpenExportSink::GetSingleton()->IsUninstalled()) return;
    if (const auto ui = RE::UI::GetSingleton()) {
        ui->AddEventSink(MainMenuOpenExportSink::GetSingleton());
        logger::info("[Export] Auto-export on Main Menu open enabled");
    } else {
        logger::critical("[Export] UI singleton unavailable at kDataLoaded; auto-export on Main Menu open disabled");
    }
}