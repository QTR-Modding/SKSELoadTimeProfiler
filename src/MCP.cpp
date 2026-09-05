#include "MCP.h"
#include "AssetReadProfiling.h"
#include "ChangeFormProfiling.h"
#include "LoadProfiling.h"
#include "MessagingProfiler.h"
#include "MessagingProfilerUI.h"
#include "Localization.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

#include <algorithm>
#include <vector>

void HelpMarker(const char* label, const char* desc) {
    ImGuiMCP::TextDisabled("%s", label);
    if (ImGuiMCP::IsItemHovered()) {
        ImGuiMCP::BeginTooltip();
        ImGuiMCP::TextUnformatted(desc);
        ImGuiMCP::EndTooltip();
    }
}


void MCP::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::info("SKSEMenuFramework is not installed.");
        return;
    }
    SKSEMenuFramework::SetSection(Localization::SectionUtilities);
    SKSEMenuFramework::AddSectionItem(Localization::MenuItemLoadTimeProfiler, RenderProfiler);
}

namespace {
    void RenderTimeCell(const double ms, const bool showSeconds, const char* msFormat = "%.1f") {
        if (ms < 0.0)
            ImGuiMCP::TextDisabled("-");
        else
            ImGuiMCP::Text(showSeconds ? "%.2f" : msFormat, showSeconds ? ms / std::milli::den : ms);
    }

    void RenderLoadTimesHelp(const bool showSeconds) {
        ImGuiMCP::TextDisabled("(?)");
        if (!ImGuiMCP::IsItemHovered() || !ImGuiMCP::BeginTooltip()) return;

        ImGuiMCP::TextUnformatted(showSeconds ? "Times are in seconds. '-' means unavailable."
                                            : "Times are in milliseconds. '-' means unavailable.");
        constexpr int columnCount = 2;
        if (ImGuiMCP::BeginTable("##loadtimes-help", columnCount, ImGuiMCP::ImGuiTableFlags_SizingFixedFit)) {
            constexpr const char* descriptions[][columnCount] = {
                {"Deser", "Reading and restoring the save."},
                {"Pre-form", "Before Skyrim starts restoring saved changes."},
                {"Change-forms", "Restoring saved changes to objects and other records."},
                {"Global-data", "Loading world data, objects and 3D."},
                {"Papyrus", "Restoring saved scripts; included in Global-data."},
                {"Menu", "How long the loading screen was visible."},
                {"In-control", "Until Skyrim reports the game loaded; not an input check."},
                {"Trailing", "After save restoration, until the loading screen closes."},
            };
            for (const auto& [label, description] : descriptions) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableNextColumn();
                ImGuiMCP::TextUnformatted(label);
                ImGuiMCP::TableNextColumn();
                ImGuiMCP::TextUnformatted(description);
            }
            ImGuiMCP::EndTable();
        }
        ImGuiMCP::TextUnformatted("Timings overlap; do not add all columns together.");
        ImGuiMCP::EndTooltip();
    }

    // Renders the save-load -> in-game timings captured by LoadProfiling.
    void RenderSaveLoadTimes(const bool showSeconds) {
        const auto loads = LoadProfiling::Snapshot();
        if (!ImGuiMCP::CollapsingHeader("Game Load Times")) return;

        if (loads.empty()) {
            ImGuiMCP::TextDisabled("No save loaded yet this session.");
            return;
        }

        ImGuiMCP::TextDisabled("Times in %s", showSeconds ? "seconds" : "milliseconds");
        ImGuiMCP::SameLine();
        RenderLoadTimesHelp(showSeconds);

        // No ScrollX: stretch all columns to fit so none scroll off-screen.
        // Units are shown above; headers stay short to keep 11 columns readable.
        if (ImGuiMCP::BeginTable("##loadtimes", 11,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable)) {
            ImGuiMCP::TableSetupColumn("Name", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
            ImGuiMCP::TableSetupColumn("Type");
            ImGuiMCP::TableSetupColumn("Deser");
            ImGuiMCP::TableSetupColumn("Pre-form");
            ImGuiMCP::TableSetupColumn("Change-forms");
            ImGuiMCP::TableSetupColumn("Global-data");
            ImGuiMCP::TableSetupColumn("Papyrus");
            ImGuiMCP::TableSetupColumn("Menu");
            ImGuiMCP::TableSetupColumn("In-control");
            ImGuiMCP::TableSetupColumn("Trailing");
            ImGuiMCP::TableSetupColumn("Result");
            ImGuiMCP::TableHeadersRow();

            for (const auto& load : loads) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", load.name.empty() ? "<unknown>" : load.name.c_str());
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%s", load.kind.c_str());
                ImGuiMCP::TableSetColumnIndex(2);
                RenderTimeCell(load.deserializeMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(3);
                RenderTimeCell(load.preFormMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(4);
                RenderTimeCell(load.formSpanMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(5);
                RenderTimeCell(load.globalDataMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(6);
                RenderTimeCell(load.papyrusMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(7);
                RenderTimeCell(load.menuVisibleMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(8);
                RenderTimeCell(load.inControlMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(9);
                RenderTimeCell(load.postToCloseMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(10);
                if (load.success)
                    ImGuiMCP::Text("ok");
                else
                    ImGuiMCP::TextDisabled("FAILED");
            }
            ImGuiMCP::EndTable();
        }
    }
}

namespace {
    // Per-DLL save-load cost: each plugin's time in its kPreLoadGame + kPostLoadGame
    // handlers (the startup table filters out "...Game" messages, so they surface here).
    void RenderPerDllLoadCost(const bool showSeconds) {
        using MI = SKSE::MessagingInterface;
        if (!ImGuiMCP::CollapsingHeader("Per-DLL Load-Game Cost")) return;

        HelpMarker("(?)",
                   "Average time each SKSE plugin spends in its own kPreLoadGame + kPostLoadGame\n"
                   "handlers per load. This is mod-attributed load work the messaging profiler\n"
                   "already records (synchronous callback time only).");

        struct Row {
            std::string module;
            double      preMs{0.0};
            double      postMs{0.0};
        };
        std::vector<Row> rows;
        for (const auto& r : MessagingProfiler::GetTaggedRows()) {
            if (r.kind != MessagingProfiler::SourceKind::DLL) continue;
            const double pre = r.perMsg[MI::kPreLoadGame];
            const double post = r.perMsg[MI::kPostLoadGame];
            if (pre + post < 0.05) continue;  // skip negligible
            rows.push_back({r.module, pre, post});
        }
        std::ranges::sort(rows, [](const Row& a, const Row& b) {
            return (a.preMs + a.postMs) > (b.preMs + b.postMs);
        });

        if (rows.empty()) {
            ImGuiMCP::TextDisabled("No DLL load-game work recorded yet.");
            return;
        }

        // Explicit outer_size so the ScrollY region has a real height (a stacked table
        // with outer_size=0 collapses to a couple of rows under a CollapsingHeader).
        if (ImGuiMCP::BeginTable("##dllloadcost", 4,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollY,
                                        ImGuiMCP::ImVec2(0.0f, 200.0f))) {
            ImGuiMCP::TableSetupColumn("DLL");
            ImGuiMCP::TableSetupColumn(showSeconds ? "PreLoadGame (s)" : "PreLoadGame (ms)");
            ImGuiMCP::TableSetupColumn(showSeconds ? "PostLoadGame (s)" : "PostLoadGame (ms)");
            ImGuiMCP::TableSetupColumn(
                (showSeconds ? Localization::TotalSecondsLabel : Localization::TotalMillisecondsLabel).c_str());
            ImGuiMCP::TableHeadersRow();
            for (const auto& row : rows) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", row.module.c_str());
                ImGuiMCP::TableSetColumnIndex(1);
                RenderTimeCell(row.preMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(2);
                RenderTimeCell(row.postMs, showSeconds);
                ImGuiMCP::TableSetColumnIndex(3);
                RenderTimeCell(row.preMs + row.postMs, showSeconds);
            }
            ImGuiMCP::EndTable();
        }
    }
}

namespace {
    // Per-mod change-form deserialize cost for the most recent load (ChangeFormProfiling).
    // Mirrors the "Change-forms by mod" export section.
    void RenderChangeFormsByMod(const bool showSeconds) {
        if (!ImGuiMCP::CollapsingHeader("Change-forms by Mod (last load)")) return;

        HelpMarker("(?)",
                   "Per-mod cost of the change-form apply loops in the most recent save load.\n"
                   "Each change-form is keyed by its FormID's load-order byte and timed by the\n"
                   "wall-clock between consecutive iterations (lookup + Revert/apply). High totals\n"
                   "here mean that mod is bloating the save. This is the breakdown of the\n"
                   "'Change-forms' deserialize sub-phase above.");

        const auto rows = ChangeFormProfiling::SnapshotLast();
        if (rows.empty()) {
            ImGuiMCP::TextDisabled("No change-form data captured yet this session.");
            return;
        }

        // Explicit outer_size so the per-mod list gets a real scroll region (a stacked
        // ScrollY table with outer_size=0 collapses to a couple of rows here).
        if (ImGuiMCP::BeginTable("##changeforms", 3,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollY,
                                        ImGuiMCP::ImVec2(0.0f, 240.0f))) {
            ImGuiMCP::TableSetupColumn("Plugin");
            ImGuiMCP::TableSetupColumn("Forms");
            ImGuiMCP::TableSetupColumn(
                (showSeconds ? Localization::TotalSecondsLabel : Localization::TotalMillisecondsLabel).c_str());
            ImGuiMCP::TableHeadersRow();
            for (const auto& r : rows) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", r.plugin.c_str());
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%llu", static_cast<unsigned long long>(r.count));
                ImGuiMCP::TableSetColumnIndex(2);
                RenderTimeCell(r.totalMs, showSeconds, "%.2f");
            }
            ImGuiMCP::EndTable();
        }
    }

    // BSA (archive) vs loose-file asset reads for the most recent load (AssetReadProfiling).
    // Mirrors the "Asset reads" export section.
    void RenderAssetReads(const bool showSeconds) {
        if (!ImGuiMCP::CollapsingHeader("Asset Reads: BSA vs Loose")) return;

        HelpMarker("(?)",
                   "Bytes and time read from BSA archives vs raw loose files during the last load.\n"
                   "Loose-file reads bypass the BSA cache and are typically far slower per byte;\n"
                   "a high loose share points at un-packed mod assets as a load-time cost.");

        const auto bsa = AssetReadProfiling::SnapshotArchive();
        const auto loose = AssetReadProfiling::SnapshotLoose();
        if (!bsa.calls && !loose.calls) {
            ImGuiMCP::TextDisabled("No asset reads captured yet this session.");
            return;
        }

        if (ImGuiMCP::BeginTable("##assetreads", 4,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable)) {
            ImGuiMCP::TableSetupColumn("Source");
            ImGuiMCP::TableSetupColumn("Reads");
            ImGuiMCP::TableSetupColumn("MB");
            ImGuiMCP::TableSetupColumn(showSeconds ? "Time (s)" : "Time (ms)");
            ImGuiMCP::TableHeadersRow();
            const auto row = [showSeconds](const char* label, const AssetReadProfiling::Stats& s) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", label);
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%llu", static_cast<unsigned long long>(s.calls));
                ImGuiMCP::TableSetColumnIndex(2);
                ImGuiMCP::Text("%.2f", static_cast<double>(s.bytes) / (1024.0 * 1024.0));
                ImGuiMCP::TableSetColumnIndex(3);
                constexpr double nanosecondsPerMillisecond = std::nano::den / std::milli::den;
                RenderTimeCell(s.totalNs / nanosecondsPerMillisecond, showSeconds);
            };
            row("BSA", bsa);
            row("Loose", loose);
            ImGuiMCP::EndTable();
        }
    }
}

void __stdcall MCP::RenderProfiler() {
    MessagingProfilerUI::State& state = MessagingProfilerUI::GetState();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs, showDllEntries, showEspEntries);
    ImGuiMCP::Separator();
    RenderSaveLoadTimes(state.showSeconds);
    RenderChangeFormsByMod(state.showSeconds);
    RenderAssetReads(state.showSeconds);
    RenderPerDllLoadCost(state.showSeconds);
}