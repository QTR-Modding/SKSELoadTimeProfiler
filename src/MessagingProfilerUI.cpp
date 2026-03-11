#include "MessagingProfilerUI.h"
#include "ESPProfiling.h"
#include "MCP.h"
#include "MessagingProfiler.h"
#include "Export.h"
#include "Settings.h"
#include "Utils.h"
#include "Localization.h"
#include <fmt/printf.h>
#include "SKSEMCP/SKSEMenuFramework.hpp"


bool MessagingProfilerUI::QueryVersionString(const wchar_t* path, const wchar_t* key, std::wstring& out) {
    DWORD handle = 0;
    const DWORD sz = GetFileVersionInfoSizeW(path, &handle);
    if (!sz) return false;
    std::vector<char> buf(sz);
    if (!GetFileVersionInfoW(path, 0, sz, buf.data())) return false;
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* lpTranslate = nullptr;
    std::uint32_t cbTranslate = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&lpTranslate),
                        &cbTranslate) ||
        cbTranslate < sizeof(LANGANDCODEPAGE)) {
        static LANGANDCODEPAGE fallback{0x0409, 1200};
        lpTranslate = &fallback;
        cbTranslate = sizeof(LANGANDCODEPAGE);
    }
    wchar_t query[256]{};
    for (UINT i = 0; i < cbTranslate / sizeof(LANGANDCODEPAGE); ++i) {
        _snwprintf_s(query, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s", lpTranslate[i].wLanguage,
                     lpTranslate[i].wCodePage, key);
        void* val = nullptr;
        std::uint32_t bytes = 0;
        if (VerQueryValueW(buf.data(), query, &val, &bytes) && val && bytes) {
            out.assign(static_cast<const wchar_t*>(val));
            return true;
        }
    }
    return false;
}

MessagingProfilerUI::DllMeta MessagingProfilerUI::GetDllMeta(const std::string& moduleBase) {
    DllMeta meta;
    const std::string dllName = moduleBase + ".dll";
    const HMODULE h = GetModuleHandleA(dllName.c_str());
    if (!h) return meta;
    wchar_t wpath[MAX_PATH]{};
    if (!GetModuleFileNameW(h, wpath, MAX_PATH)) return meta;
    std::wstring w;
    if (QueryVersionString(wpath, L"CompanyName", w) || QueryVersionString(wpath, L"Author", w))
        meta.author = Utilities::WideToUtf8(w);
    if (QueryVersionString(wpath, L"ProductVersion", w) || QueryVersionString(wpath, L"FileVersion", w))
        meta.version = Utilities::WideToUtf8(w);
    if (QueryVersionString(wpath, L"License", w) || QueryVersionString(wpath, L"LegalCopyright", w))
        meta.license = Utilities::WideToUtf8(w);
    meta.ok = !(meta.author.empty() && meta.version.empty() && meta.license.empty());
    return meta;
}

namespace {
    template <class... Args>
    std::string FormatLocalized(const std::string& format, Args&&... args) {
        try {
            return fmt::sprintf(format, std::forward<Args>(args)...);
        } catch (const fmt::format_error& e) {
            logger::warn("[Localization] Invalid format string '{}': {}", format, e.what());
            return {};
        }
    }

    struct RowWrap {
        std::string module;
        std::string_view typeStr;
        double total;
        const std::array<double, SKSE::MessagingInterface::kTotal>* vals;
        bool isEsp;
    };

    struct RenderRows {
        std::vector<RowWrap> rows;
    };

    bool CaseInsensitiveContains(const std::string_view haystack, const std::string_view needle) {
        if (needle.empty()) return true;
        if (haystack.size() < needle.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                const auto hc = static_cast<unsigned char>(haystack[i + j]);
                const auto nc = static_cast<unsigned char>(needle[j]);
                if (static_cast<char>(std::tolower(hc)) != static_cast<char>(std::tolower(nc))) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
        return false;
    }

    std::string_view GetMessageTypeTooltip(const std::size_t index) {
        return Localization::MessageTypeTooltip(index);
    }

    float ComputeSummaryLabelWidth() {
        const char* labels[] = {
            Localization::SkseInitTimeHeuristic.c_str(),
            Localization::TotalDllTime.c_str(),
            Localization::TotalEspTime.c_str(),
            Localization::TotalTime.c_str(),
            Localization::CurrentlyLoadingDll.c_str(),
            Localization::CurrentlyLoadingEsp.c_str(),
        };

        float labelWidth = 0.0f;
        for (const char* label : labels) {
            ImGuiMCP::ImVec2 size{};
            ImGuiMCP::ImGui::CalcTextSize(&size, label, nullptr, false, 0.0f);
            labelWidth = std::max(labelWidth, size.x);
        }

        const auto style = ImGuiMCP::ImGui::GetStyle();
        return labelWidth + (style ? style->ItemSpacing.x : 0.0f);
    }

    void RenderSummaryCurrentLoadingValue(const std::string& name, const double elapsedMs, const bool showSeconds) {
        if (name.empty()) {
            ImGuiMCP::ImGui::TextUnformatted(Localization::PlaceholderEmpty.c_str());
            return;
        }

        if (elapsedMs < 0.0) {
            ImGuiMCP::ImGui::TextUnformatted(name.c_str());
            return;
        }

        if (showSeconds) {
            if (elapsedMs >= 1000.0)
                ImGuiMCP::ImGui::Text("%s (%.0f s)", name.c_str(), elapsedMs * 0.001);
            else
                ImGuiMCP::ImGui::TextUnformatted(name.c_str());
        } else {
            if (elapsedMs >= 1.0)
                ImGuiMCP::ImGui::Text("%s (%.0f ms)", name.c_str(), elapsedMs);
            else
                ImGuiMCP::ImGui::TextUnformatted(name.c_str());
        }
    }

    void RenderSummaryCurrentLoadingRow(const char* label, const std::string& name, const double elapsedMs,
                                        const bool showSeconds) {
        ImGuiMCP::ImGui::TableNextRow();
        ImGuiMCP::ImGui::TableSetColumnIndex(0);
        ImGuiMCP::ImGui::TextUnformatted(label);
        ImGuiMCP::ImGui::TableSetColumnIndex(1);
        RenderSummaryCurrentLoadingValue(name, elapsedMs, showSeconds);
    }

    void RenderSummaryActions(MessagingProfilerUI::State& s) {
        const auto exportButtonLabel = Localization::MakeLabel(Localization::ExportButton, "export-button");
        if (ImGuiMCP::ImGui::Button(exportButtonLabel.c_str())) {
            auto format = Export::Format::Csv;
            if (s.exportFormat == static_cast<int>(Export::Format::Txt)) {
                format = Export::Format::Txt;
            } else if (s.exportFormat == static_cast<int>(Export::Format::Json)) {
                format = Export::Format::Json;
            }
            Export::WriteSnapshot(format, s.exportStatus);
        }

        ImGuiMCP::ImGui::SameLine();
        ImGuiMCP::ImGui::SetNextItemWidth(120.0f);
        const auto exportFormatLabel = Localization::MakeLabel("", "export-format");
        const char* formats[] = {Localization::ExportFormatCsv.c_str(), Localization::ExportFormatTxt.c_str(),
                                 Localization::ExportFormatJson.c_str()};
        ImGuiMCP::ImGui::Combo(exportFormatLabel.c_str(), &s.exportFormat, formats, 3);

        if (!s.exportStatus.empty()) {
            ImGuiMCP::ImGui::SameLine();
            ImGuiMCP::ImGui::TextUnformatted(s.exportStatus.c_str());
        }
    }

    void RenderResultsToolbar(MessagingProfilerUI::State& s, bool& showDllEntries, bool& showEspEntries) {
        ImGuiMCP::ImGui::SetNextItemWidth(260.0f);
        const auto searchLabel = Localization::MakeLabel(Localization::SearchLabel, "search");
        ImGuiMCP::ImGui::InputTextWithHint(searchLabel.c_str(), Localization::SearchHint.c_str(), s.search.data(),
                                           s.search.size());
        ImGuiMCP::ImGui::SameLine();

        bool dll = showDllEntries;
        const auto dllLabel = Localization::MakeLabel(Localization::TypeDll, "filter-dll");
        if (ImGuiMCP::ImGui::Checkbox(dllLabel.c_str(), &dll)) {
            showDllEntries = dll;
            Settings::Save();
        }

        ImGuiMCP::ImGui::SameLine();
        bool esp = showEspEntries;
        const auto espLabel = Localization::MakeLabel(Localization::TypeEsp, "filter-esp");
        if (ImGuiMCP::ImGui::Checkbox(espLabel.c_str(), &esp)) {
            showEspEntries = esp;
            Settings::Save();
        }
    }

    void RenderSummary(const MessagingProfilerUI::State& s,
                       const std::vector<MessagingProfiler::TaggedRow>& taggedRows) {
        ImGuiMCP::ImGui::TextUnformatted(Localization::Summary.c_str());
        double totalEspMs = 0.0;
        double totalDllMs = 0.0;
        for (const auto& row : taggedRows) {
            if (row.kind == MessagingProfiler::SourceKind::ESP) {
                totalEspMs += row.totalMs;
            } else {
                for (const double v : row.perMsg) {
                    if (v >= 1.0) totalDllMs += v;
                }
            }
        }
        const double loadMs = MCP::loadTimeMs.load(std::memory_order_relaxed);
        if (loadMs >= 0.0) totalDllMs += loadMs;
        const double totalAllMs = totalDllMs + totalEspMs;
        const double displayScale = s.showSeconds ? 0.001 : 1.0;
        const char* displayFmt = s.showSeconds ? "%.2f" : "%.0f";
        if (ImGuiMCP::ImGui::BeginTable("##prof-summary", 2,
                                        ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                                        ImGuiMCP::ImGuiTableFlags_BordersInnerV)) {
            ImGuiMCP::ImGui::TableSetupColumn("##summary-labels",
                                              ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, ComputeSummaryLabelWidth());
            ImGuiMCP::ImGui::TableSetupColumn("##summary-values",
                                              ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
            ImGuiMCP::ImGui::TableNextRow();
            ImGuiMCP::ImGui::TableSetColumnIndex(0);
            ImGuiMCP::ImGui::TextUnformatted(Localization::SkseInitTimeHeuristic.c_str());
            ImGuiMCP::ImGui::TableSetColumnIndex(1);
            if (loadMs >= 0.0) {
                if (s.showSeconds) {
                    const auto text = FormatLocalized(Localization::FormatSeconds, loadMs * 0.001);
                    ImGuiMCP::ImGui::TextUnformatted(text.c_str());
                } else {
                    const auto text = FormatLocalized(Localization::FormatMilliseconds, loadMs);
                    ImGuiMCP::ImGui::TextUnformatted(text.c_str());
                }
            } else {
                ImGuiMCP::ImGui::TextUnformatted(Localization::PlaceholderEmpty.c_str());
            }

            ImGuiMCP::ImGui::TableNextRow();
            ImGuiMCP::ImGui::TableSetColumnIndex(0);
            ImGuiMCP::ImGui::TextUnformatted(Localization::TotalDllTime.c_str());
            ImGuiMCP::ImGui::TableSetColumnIndex(1);
            ImGuiMCP::ImGui::Text(displayFmt, totalDllMs * displayScale);

            ImGuiMCP::ImGui::TableNextRow();
            ImGuiMCP::ImGui::TableSetColumnIndex(0);
            ImGuiMCP::ImGui::TextUnformatted(Localization::TotalEspTime.c_str());
            ImGuiMCP::ImGui::TableSetColumnIndex(1);
            ImGuiMCP::ImGui::Text(displayFmt, totalEspMs * displayScale);

            ImGuiMCP::ImGui::TableNextRow();
            ImGuiMCP::ImGui::TableSetColumnIndex(0);
            ImGuiMCP::ImGui::TextUnformatted(Localization::TotalTime.c_str());
            ImGuiMCP::ImGui::TableSetColumnIndex(1);
            ImGuiMCP::ImGui::Text(displayFmt, totalAllMs * displayScale);

            const auto currentDll = MessagingProfiler::GetCurrentCallbackModule();
            const double currentDllMs = MessagingProfiler::GetCurrentCallbackElapsedMs();
            const auto currentEsp = ESPProfiling::GetCurrentLoading();
            const double currentEspMs = ESPProfiling::GetCurrentLoadingElapsedMs();
            RenderSummaryCurrentLoadingRow(Localization::CurrentlyLoadingDll.c_str(), currentDll, currentDllMs,
                                           s.showSeconds);
            RenderSummaryCurrentLoadingRow(Localization::CurrentlyLoadingEsp.c_str(), currentEsp, currentEspMs,
                                           s.showSeconds);
            ImGuiMCP::ImGui::EndTable();
        }
    }

    void RenderMessageTypeSelector(MessagingProfilerUI::State& s, const std::vector<std::string_view>& names) {
        ImGuiMCP::ImGui::AlignTextToFramePadding();
        ImGuiMCP::ImGui::TextUnformatted(Localization::Selection.c_str());
        ImGuiMCP::ImGui::SameLine();
        const auto allLabel = Localization::MakeLabel(Localization::ButtonAll, "all");
        if (ImGuiMCP::ImGui::Button(allLabel.c_str())) {
            std::ranges::fill(s.selected, true);
            Settings::Save();
        }
        ImGuiMCP::ImGui::SameLine();
        const auto noneLabel = Localization::MakeLabel(Localization::ButtonNone, "none");
        if (ImGuiMCP::ImGui::Button(noneLabel.c_str())) {
            std::ranges::fill(s.selected, false);
            Settings::Save();
        }

        ImGuiMCP::ImGui::Spacing();
        ImGuiMCP::ImVec2 avail{};
        ImGuiMCP::ImGui::GetContentRegionAvail(&avail);
        const int columns = (avail.x > 520.0f) ? 4 : 3;
        if (ImGuiMCP::ImGui::BeginTable("##msgtypes-grid", columns,
                                        ImGuiMCP::ImGuiTableFlags_SizingStretchSame)) {
            for (std::size_t i = 0; i < names.size(); ++i) {
                ImGuiMCP::ImGui::TableNextColumn();
                ImGuiMCP::ImGui::PushID(static_cast<int>(i));
                bool sel = s.selected[i];
                const auto& label = Localization::MessageTypeLabel(i);
                if (ImGuiMCP::ImGui::Checkbox(label.c_str(), &sel)) {
                    s.selected[i] = sel;
                    Settings::Save();
                }
                const auto tooltip = GetMessageTypeTooltip(i);
                if (!tooltip.empty() && ImGuiMCP::ImGui::IsItemHovered()) {
                    ImGuiMCP::ImGui::BeginTooltip();
                    ImGuiMCP::ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
                    ImGuiMCP::ImGui::EndTooltip();
                }
                ImGuiMCP::ImGui::PopID();
            }
            ImGuiMCP::ImGui::EndTable();
        }
    }

    void RenderControls(MessagingProfilerUI::State& s, const std::vector<std::string_view>& names, double& warnMs,
                        double& critMs) {
        const auto displayLabel = Localization::MakeLabel(Localization::HeaderDisplay, "display");
        if (ImGuiMCP::ImGui::CollapsingHeader(displayLabel.c_str())) {
            const auto saveLabel = Localization::MakeLabel(Localization::ButtonSaveSettings, "save-settings");
            const bool saveRequested = ImGuiMCP::ImGui::Button(saveLabel.c_str());
            const auto secondsLabel = Localization::MakeLabel(Localization::CheckShowInSeconds, "show-seconds");
            ImGuiMCP::ImGui::Checkbox(secondsLabel.c_str(), &s.showSeconds);
            ImGuiMCP::ImGui::SameLine();
            HelpMarker(Localization::HelpMarkerLabel.c_str(), Localization::HelpMarkerSeconds.c_str());
            ImGuiMCP::ImGui::Spacing();

            bool thresholdsDirty = false;
            ImGuiMCP::ImGui::TextUnformatted(Localization::Thresholds.c_str());
            ImGuiMCP::ImGui::SameLine();
            HelpMarker(Localization::HelpMarkerLabel.c_str(), Localization::HelpMarkerThresholds.c_str());
            ImGuiMCP::ImGui::PushID("prof-thresholds");
            ImGuiMCP::ImGui::SetNextItemWidth(140);
            float warn = static_cast<float>(warnMs);
            const auto warnLabel = Localization::MakeLabel(Localization::WarnMs, "warn-ms");
            if (ImGuiMCP::ImGui::DragFloat(warnLabel.c_str(), &warn, 10.f, 0.f, 10000.f, "%.0f")) {
                warnMs = warn;
                thresholdsDirty = true;
            }
            ImGuiMCP::ImGui::SameLine();
            ImGuiMCP::ImGui::SetNextItemWidth(140);
            float crit = static_cast<float>(critMs);
            const auto critLabel = Localization::MakeLabel(Localization::CritMs, "crit-ms");
            if (ImGuiMCP::ImGui::DragFloat(critLabel.c_str(), &crit, 10.f, 0.f, 20000.f, "%.0f")) {
                critMs = crit;
                thresholdsDirty = true;
            }
            ImGuiMCP::ImGui::PopID();
            if (saveRequested || thresholdsDirty) Settings::Save();

            ImGuiMCP::ImGui::Spacing();
        }

        const auto messageTypesLabel = Localization::MakeLabel(Localization::HeaderMessageTypes, "message-types");
        if (ImGuiMCP::ImGui::CollapsingHeader(messageTypesLabel.c_str())) {
            RenderMessageTypeSelector(s, names);
        }
    }

    std::vector<std::size_t> BuildActiveSelections(const MessagingProfilerUI::State& s) {
        std::vector<std::size_t> active;
        active.reserve(s.selected.size());
        constexpr std::size_t dataLoadedIndex = SKSE::MessagingInterface::kDataLoaded;
        if (dataLoadedIndex < s.selected.size() && s.selected[dataLoadedIndex]) {
            active.push_back(dataLoadedIndex);
        }
        for (std::size_t i = 0; i < s.selected.size(); ++i)
            if (s.selected[i] && i != dataLoadedIndex) active.push_back(i);
        return active;
    }

    RenderRows BuildEnrichedRows(const MessagingProfilerUI::State& s,
                                 const std::vector<MessagingProfiler::TaggedRow>& taggedRows,
                                 const std::vector<std::size_t>& active, const std::string_view filter,
                                 const bool showDllEntries, const bool showEspEntries) {
        RenderRows result;
        result.rows.reserve(taggedRows.size());
        for (const auto& r : taggedRows) {
            if (r.kind == MessagingProfiler::SourceKind::DLL && !showDllEntries) continue;
            if (r.kind == MessagingProfiler::SourceKind::ESP && !showEspEntries) continue;
            if (!filter.empty() && !CaseInsensitiveContains(r.module, filter)) continue;
            double sum = 0.0;
            if (r.kind == MessagingProfiler::SourceKind::ESP) {
                sum = r.totalMs;
            } else {
                for (const auto idx : active) {
                    double v = r.perMsg[idx];
                    if (v < 1.0) v = 0.0;
                    sum += v;
                }
            }
            result.rows.push_back({r.module,
                                   r.kind == MessagingProfiler::SourceKind::ESP
                                       ? Localization::TypeEsp
                                       : Localization::TypeDll,
                                   sum,
                                   &r.perMsg,
                                   r.kind == MessagingProfiler::SourceKind::ESP});
        }

        std::ranges::sort(result.rows, [&](const RowWrap& A, const RowWrap& B) {
            if (s.sortColumn == 0) return s.sortAsc ? A.module < B.module : A.module > B.module;
            if (s.sortColumn == 1)
                return s.sortAsc
                           ? std::string_view(A.typeStr) < std::string_view(B.typeStr)
                           : std::string_view(A.typeStr) > std::string_view(B.typeStr);
            if (s.sortColumn == 2) return s.sortAsc ? A.total < B.total : A.total > B.total;
            const std::size_t msgIdx = active[s.sortColumn - 3];
            double av = (*A.vals)[msgIdx];
            if (av < 1.0 || A.isEsp) av = 0.0;
            double bv = (*B.vals)[msgIdx];
            if (bv < 1.0 || B.isEsp) bv = 0.0;
            return s.sortAsc ? av < bv : av > bv;
        });

        return result;
    }

    void RenderResultsTable(MessagingProfilerUI::State& s, const std::vector<std::string_view>& names,
                            const std::vector<MessagingProfiler::TaggedRow>& taggedRows, const double warnMs,
                            const double critMs, bool& showDllEntries, bool& showEspEntries) {
        ImGuiMCP::ImGui::Separator();
        ImGuiMCP::ImGui::Spacing();
        RenderResultsToolbar(s, showDllEntries, showEspEntries);
        ImGuiMCP::ImGui::Spacing();

        ImGuiMCP::ImVec2 avail{};
        ImGuiMCP::ImGui::GetContentRegionAvail(&avail);
        const float childHeight = avail.y;

        ImGuiMCP::ImGui::BeginChild("##msgprof-results", ImGuiMCP::ImVec2(0.0f, childHeight), true);
        const auto active = BuildActiveSelections(s);
        if (active.empty()) {
            ImGuiMCP::ImGui::TextUnformatted(Localization::NoMessageTypesSelected.c_str());
            ImGuiMCP::ImGui::EndChild();
            return;
        }

        std::string_view filter = s.search.data();
        if (!filter.empty()) {
            filter = std::string_view(s.search.data(), std::strlen(s.search.data()));
        }

        if (ImGuiMCP::ImGui::BeginTable("##msgprof2", static_cast<int>(active.size()) + 3,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable |
                                        ImGuiMCP::ImGuiTableFlags_Reorderable |
                                        ImGuiMCP::ImGuiTableFlags_Sortable |
                                        ImGuiMCP::ImGuiTableFlags_ScrollX |
                                        ImGuiMCP::ImGuiTableFlags_ScrollY)) {
            const auto& totalLabel = s.showSeconds
                                         ? Localization::TotalSecondsLabel
                                         : Localization::TotalMillisecondsLabel;
            const auto moduleLabel = Localization::MakeLabel(Localization::ColumnModule, "module");
            const auto typeLabel = Localization::MakeLabel(Localization::ColumnType, "type");
            const auto totalHeader = Localization::MakeLabel(totalLabel, "total");
            ImGuiMCP::ImGui::TableSetupColumn(
                moduleLabel.c_str(),
                ImGuiMCP::ImGuiTableColumnFlags_DefaultSort | ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
            ImGuiMCP::ImGui::TableSetupColumn(typeLabel.c_str(), ImGuiMCP::ImGuiTableColumnFlags_PreferSortDescending);
            ImGuiMCP::ImGui::TableSetupColumn(totalHeader.c_str(),
                                              ImGuiMCP::ImGuiTableColumnFlags_PreferSortDescending);
            for (const auto idx : active) {
                const auto& visible = Localization::MessageTypeLabel(idx);
                const auto colLabel = Localization::MakeLabel(visible, names[idx].data());
                ImGuiMCP::ImGui::TableSetupColumn(colLabel.c_str(),
                                                  ImGuiMCP::ImGuiTableColumnFlags_PreferSortDescending);
            }
            ImGuiMCP::ImGui::TableHeadersRow();
            if (const ImGuiMCP::ImGuiTableSortSpecs* sortSpecs = ImGuiMCP::ImGui::TableGetSortSpecs())
                if (sortSpecs->SpecsCount > 0) {
                    s.sortColumn = sortSpecs->Specs[0].ColumnIndex;
                    s.sortAsc = sortSpecs->Specs[0].SortDirection == ImGuiMCP::ImGuiSortDirection_Ascending;
                }

            auto renderRows = BuildEnrichedRows(s, taggedRows, active, filter, showDllEntries, showEspEntries);
            std::vector colTotals(active.size(), 0.0);
            for (const auto& e : renderRows.rows)
                for (std::size_t c = 0; c < active.size(); ++c) {
                    double v = (*e.vals)[active[c]];
                    if (e.isEsp || v < 1.0) v = 0.0;
                    colTotals[c] += v;
                }
            double totalsSum = 0.0;
            for (const double v : colTotals) totalsSum += v;
            double espExtra = 0.0;
            for (const auto& e : renderRows.rows)
                if (e.isEsp) espExtra += e.total;
            const double grandTotal = totalsSum + espExtra;
            const double displayScale = s.showSeconds ? 0.001 : 1.0;
            const char* displayFmt = s.showSeconds ? "%.2f" : "%.0f";

            ImGuiMCP::ImGui::TableNextRow();
            const auto totalsBg = ImGuiMCP::ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_TableRowBgAlt);
            float totalsAlpha = totalsBg ? totalsBg->w + 0.15f : 0.35f;
            totalsAlpha = std::min(totalsAlpha, 0.6f);
            const auto totalsColor = ImGuiMCP::ImGui::GetColorU32(
                ImGuiMCP::ImVec4(totalsBg ? totalsBg->x : 0.2f, totalsBg ? totalsBg->y : 0.2f,
                                 totalsBg ? totalsBg->z : 0.2f, totalsAlpha));
            ImGuiMCP::ImGui::TableSetBgColor(ImGuiMCP::ImGuiTableBgTarget_RowBg0, totalsColor);
            ImGuiMCP::ImGui::TableSetColumnIndex(0);
            ImGuiMCP::ImGui::Text("%s (%llu)", Localization::TotalsRowLabel.c_str(),
                                  static_cast<unsigned long long>(renderRows.rows.size()));
            ImGuiMCP::ImGui::TableSetColumnIndex(1);
            ImGuiMCP::ImGui::TextUnformatted(Localization::PlaceholderEmpty.c_str());
            ImGuiMCP::ImGui::TableSetColumnIndex(2);
            MessagingProfilerUI::ColorCell(grandTotal, warnMs, critMs);
            ImGuiMCP::ImGui::Text(displayFmt, grandTotal * displayScale);
            for (std::size_t c = 0; c < active.size(); ++c) {
                ImGuiMCP::ImGui::TableSetColumnIndex(static_cast<int>(c + 3));
                const double v = colTotals[c];
                MessagingProfilerUI::ColorCell(v, warnMs, critMs);
                ImGuiMCP::ImGui::Text(displayFmt, v * displayScale);
            }

            static auto clipper = ImGuiMCP::ImGui::ImGuiListClipperManager::Create();
            if (clipper) {
                ImGuiMCP::ImGui::ImGuiListClipperManager::Begin(
                    clipper, static_cast<int>(renderRows.rows.size()), 0.0f);
                while (ImGuiMCP::ImGui::ImGuiListClipperManager::Step(clipper)) {
                    for (int i = clipper->DisplayStart; i < clipper->DisplayEnd; ++i) {
                        auto& e = renderRows.rows[static_cast<std::size_t>(i)];
                        ImGuiMCP::ImGui::TableNextRow();
                        ImGuiMCP::ImGui::TableSetColumnIndex(0);
                        ImGuiMCP::ImGui::TextUnformatted(e.module.c_str());
                        if (!e.isEsp && ImGuiMCP::ImGui::IsItemHovered()) {
                            auto it = MessagingProfilerUI::g_metaCache.find(e.module);
                            if (it == MessagingProfilerUI::g_metaCache.end())
                                it = MessagingProfilerUI::g_metaCache.emplace(
                                    e.module, MessagingProfilerUI::GetDllMeta(e.module)).first;
                            if (ImGuiMCP::ImGui::BeginTooltip()) {
                                ImGuiMCP::ImGui::TextUnformatted(e.module.c_str());
                                if (it->second.ok) {
                                    if (!it->second.author.empty()) {
                                        ImGuiMCP::ImGui::Text("%s: %s", Localization::Author.c_str(),
                                                              it->second.author.c_str());
                                    }
                                    if (!it->second.version.empty()) {
                                        ImGuiMCP::ImGui::Text("%s: %s", Localization::Version.c_str(),
                                                              it->second.version.c_str());
                                    }
                                    if (!it->second.license.empty()) {
                                        ImGuiMCP::ImGui::Text("%s: %s", Localization::License.c_str(),
                                                              it->second.license.c_str());
                                    }
                                } else {
                                    ImGuiMCP::ImGui::TextUnformatted(Localization::TooltipNoVersionInfo.c_str());
                                }
                                ImGuiMCP::ImGui::EndTooltip();
                            }
                        }
                        ImGuiMCP::ImGui::TableSetColumnIndex(1);
                        ImGuiMCP::ImGui::TextUnformatted(e.typeStr.data(), e.typeStr.data() + e.typeStr.size());
                        ImGuiMCP::ImGui::TableSetColumnIndex(2);
                        MessagingProfilerUI::ColorCell(e.total, warnMs, critMs);
                        ImGuiMCP::ImGui::Text(displayFmt, e.total * displayScale);
                        for (std::size_t c = 0; c < active.size(); ++c) {
                            ImGuiMCP::ImGui::TableSetColumnIndex(static_cast<int>(c + 3));
                            double v = (*e.vals)[active[c]];
                            if (v < 1.0 || e.isEsp) v = 0.0;
                            if (!e.isEsp) MessagingProfilerUI::ColorCell(v, warnMs, critMs);
                            ImGuiMCP::ImGui::Text(displayFmt, v * displayScale);
                        }
                    }
                }
                ImGuiMCP::ImGui::ImGuiListClipperManager::End(clipper);
            }
            ImGuiMCP::ImGui::EndTable();
        }
        ImGuiMCP::ImGui::EndChild();

        ImGuiMCP::ImGui::Spacing();
    }
}

void MessagingProfilerUI::Render(State& s, double& warnMs, double& critMs, bool& showDllEntries, bool& showEspEntries) {
    static const auto names = MessagingProfiler::GetMessageTypeNames();
    const auto taggedRows = MessagingProfiler::GetTaggedRows();
    EnsureSelectionSize(s, names.size());

    if (!s.initializedFromDisk && !s.selected.empty()) {
        bool any = false;
        for (const bool b : s.selected) {
            if (b) {
                any = true;
                break;
            }
        }
        if (!any) std::ranges::fill(s.selected, true);
    }

    RenderSummary(s, taggedRows);
    RenderSummaryActions(s);
    ImGuiMCP::ImGui::Spacing();
    RenderControls(s, names, warnMs, critMs);
    RenderResultsTable(s, names, taggedRows, warnMs, critMs, showDllEntries, showEspEntries);
}

void MessagingProfilerUI::ColorCell(const double v, const double warnMs, const double critMs) {
    ImGuiMCP::ImU32 col = 0;
    if (v >= critMs)
        col = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(0.85f, 0.15f, 0.15f, 0.35f));
    else if (v >= warnMs)
        col = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(0.95f, 0.75f, 0.10f, 0.25f));
    if (col) ImGuiMCP::ImGui::TableSetBgColor(ImGuiMCP::ImGuiTableBgTarget_CellBg, col);
}

