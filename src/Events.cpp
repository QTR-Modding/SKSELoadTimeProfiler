#include "Events.h"
#include "Export.h"
#include "Hooks.h"
#include "MessagingProfiler.h"
#include "REX/REX/Singleton.h"

class MainMenuOpenExportSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                                     public REX::Singleton<MainMenuOpenExportSink> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

    bool IsUninstalled() const noexcept { return uninstalled; }

private:
    bool uninstalled = false;
    RE::BSEventNotifyControl UnInstall();
};

RE::BSEventNotifyControl MainMenuOpenExportSink::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (uninstalled || !event || !event->opening) return RE::BSEventNotifyControl::kContinue;
    if (event->menuName != RE::MainMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;

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
    if (csvOk && txtOk)
        logger::info("[Export] Auto-generated CSV and TXT snapshots on Main Menu open");
    else
        logger::warn("[Export] Auto-export incomplete on Main Menu open (csv={}, txt={})", csvOk, txtOk);

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