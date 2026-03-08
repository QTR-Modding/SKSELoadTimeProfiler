#include "Localization.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

namespace {
    std::filesystem::path GetLocalizationPath() {
        return R"(Data/SKSE/Plugins/LoadTimeProfiler/Localization.json)";
    }

    std::string GetOrWarn(const rapidjson::Document& doc, const char* key) {
        if (!doc.IsObject()) {
            logger::warn("[Localization] Translation file is not a JSON object");
            return {};
        }
        if (const auto it = doc.FindMember(key); it != doc.MemberEnd() && it->value.IsString()) {
            return it->value.GetString();
        }
        logger::warn("[Localization] Missing translation for key '{}'", key);
        return {};
    }
}

void Localization::Load() {
    const auto path = GetLocalizationPath();
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        logger::warn("[Localization] Failed to open translation file '{}'", path.string());
        return;
    }

    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document doc;
    doc.ParseStream(isw);
    if (doc.HasParseError() || !doc.IsObject()) {
        logger::warn("[Localization] Failed to parse translation file '{}'", path.string());
        return;
    }

    SectionUtilities = GetOrWarn(doc, "$ltpSectionUtilities");
    MenuItemLoadTimeProfiler = GetOrWarn(doc, "$ltpMenuItem");
    Summary = GetOrWarn(doc, "$ltpSummary");
    SkseInitTimeHeuristic = GetOrWarn(doc, "$ltpSkseInitTimeHeuristic");
    TotalDllTime = GetOrWarn(doc, "$ltpTotalDllTime");
    TotalEspTime = GetOrWarn(doc, "$ltpTotalEspTime");
    TotalTime = GetOrWarn(doc, "$ltpTotalTime");
    CurrentlyLoadingDll = GetOrWarn(doc, "$ltpCurrentlyLoadingDll");
    CurrentlyLoadingEsp = GetOrWarn(doc, "$ltpCurrentlyLoadingEsp");
    Selection = GetOrWarn(doc, "$ltpSelection");
    ButtonAll = GetOrWarn(doc, "$ltpAll");
    ButtonNone = GetOrWarn(doc, "$ltpNone");
    HeaderDisplay = GetOrWarn(doc, "$ltpDisplay");
    ButtonSaveSettings = GetOrWarn(doc, "$ltpSaveSettings");
    CheckShowInSeconds = GetOrWarn(doc, "$ltpShowInSeconds");
    HelpMarkerLabel = GetOrWarn(doc, "$ltpHelpMarkerLabel");
    HelpMarkerSeconds = GetOrWarn(doc, "$ltpHelpMarkerSeconds");
    Thresholds = GetOrWarn(doc, "$ltpThresholds");
    HelpMarkerThresholds = GetOrWarn(doc, "$ltpHelpMarkerThresholds");
    WarnMs = GetOrWarn(doc, "$ltpWarnMs");
    CritMs = GetOrWarn(doc, "$ltpCritMs");
    HeaderMessageTypes = GetOrWarn(doc, "$ltpMessageTypes");
    SearchLabel = GetOrWarn(doc, "$ltpSearch");
    SearchHint = GetOrWarn(doc, "$ltpSearchHint");
    ExportFormatCsv = GetOrWarn(doc, "$ltpExportFormatCsv");
    ExportFormatTxt = GetOrWarn(doc, "$ltpExportFormatTxt");
    ExportButton = GetOrWarn(doc, "$ltpExportButton");
    ExportStatusSuccess = GetOrWarn(doc, "$ltpExportStatusSuccess");
    ExportStatusFailed = GetOrWarn(doc, "$ltpExportStatusFailed");
    TypeDll = GetOrWarn(doc, "$ltpTypeDll");
    TypeEsp = GetOrWarn(doc, "$ltpTypeEsp");
    NoMessageTypesSelected = GetOrWarn(doc, "$ltpNoMessageTypesSelected");
    TotalSecondsLabel = GetOrWarn(doc, "$ltpTotalSeconds");
    TotalMillisecondsLabel = GetOrWarn(doc, "$ltpTotalMilliseconds");
    ColumnModule = GetOrWarn(doc, "$ltpColumnModule");
    ColumnType = GetOrWarn(doc, "$ltpColumnType");
    TotalsRowLabel = GetOrWarn(doc, "$ltpTotalsRow");
    PlaceholderEmpty = GetOrWarn(doc, "$ltpPlaceholderEmpty");
    TooltipAuthor = GetOrWarn(doc, "$ltpTooltipAuthor");
    TooltipVersion = GetOrWarn(doc, "$ltpTooltipVersion");
    TooltipLicense = GetOrWarn(doc, "$ltpTooltipLicense");
    TooltipNoVersionInfo = GetOrWarn(doc, "$ltpTooltipNoVersionInfo");
    FormatSeconds = GetOrWarn(doc, "$ltpFormatSeconds");
    FormatMilliseconds = GetOrWarn(doc, "$ltpFormatMilliseconds");
    MsgPostLoad = GetOrWarn(doc, "$ltpMsgPostLoad");
    MsgPostPostLoad = GetOrWarn(doc, "$ltpMsgPostPostLoad");
    MsgPreLoadGame = GetOrWarn(doc, "$ltpMsgPreLoadGame");
    MsgPostLoadGame = GetOrWarn(doc, "$ltpMsgPostLoadGame");
    MsgSaveGame = GetOrWarn(doc, "$ltpMsgSaveGame");
    MsgDeleteGame = GetOrWarn(doc, "$ltpMsgDeleteGame");
    MsgInputLoaded = GetOrWarn(doc, "$ltpMsgInputLoaded");
    MsgNewGame = GetOrWarn(doc, "$ltpMsgNewGame");
    MsgDataLoaded = GetOrWarn(doc, "$ltpMsgDataLoaded");
    MsgUnknown = GetOrWarn(doc, "$ltpMsgUnknown");
    TipPostLoad = GetOrWarn(doc, "$ltpTipPostLoad");
    TipPostPostLoad = GetOrWarn(doc, "$ltpTipPostPostLoad");
    TipPreLoadGame = GetOrWarn(doc, "$ltpTipPreLoadGame");
    TipPostLoadGame = GetOrWarn(doc, "$ltpTipPostLoadGame");
    TipSaveGame = GetOrWarn(doc, "$ltpTipSaveGame");
    TipDeleteGame = GetOrWarn(doc, "$ltpTipDeleteGame");
    TipInputLoaded = GetOrWarn(doc, "$ltpTipInputLoaded");
    TipNewGame = GetOrWarn(doc, "$ltpTipNewGame");
    TipDataLoaded = GetOrWarn(doc, "$ltpTipDataLoaded");
    Empty.clear();
}

std::string Localization::MakeLabel(const std::string& visible, const char* stableId) {
    std::string label;
    label.reserve(visible.size() + 2 + std::strlen(stableId));
    label.append(visible);
    label.append("##");
    label.append(stableId);
    return label;
}

const std::string& Localization::MessageTypeLabel(const std::size_t index) {
    using MI = SKSE::MessagingInterface;
    switch (index) {
        case MI::kPostLoad:
            return MsgPostLoad;
        case MI::kPostPostLoad:
            return MsgPostPostLoad;
        case MI::kPreLoadGame:
            return MsgPreLoadGame;
        case MI::kPostLoadGame:
            return MsgPostLoadGame;
        case MI::kSaveGame:
            return MsgSaveGame;
        case MI::kDeleteGame:
            return MsgDeleteGame;
        case MI::kInputLoaded:
            return MsgInputLoaded;
        case MI::kNewGame:
            return MsgNewGame;
        case MI::kDataLoaded:
            return MsgDataLoaded;
        default:
            return MsgUnknown;
    }
}

const std::string& Localization::MessageTypeTooltip(const std::size_t index) {
    using MI = SKSE::MessagingInterface;
    switch (index) {
        case MI::kPostLoad:
            return TipPostLoad;
        case MI::kPostPostLoad:
            return TipPostPostLoad;
        case MI::kPreLoadGame:
            return TipPreLoadGame;
        case MI::kPostLoadGame:
            return TipPostLoadGame;
        case MI::kSaveGame:
            return TipSaveGame;
        case MI::kDeleteGame:
            return TipDeleteGame;
        case MI::kInputLoaded:
            return TipInputLoaded;
        case MI::kNewGame:
            return TipNewGame;
        case MI::kDataLoaded:
            return TipDataLoaded;
        default:
            return Empty;
    }
}