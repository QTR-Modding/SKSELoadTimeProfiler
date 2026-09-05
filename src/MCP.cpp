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
    void RenderLoadCell(const double ms) {
        if (ms < 0.0)
            ImGuiMCP::TextDisabled("-");
        else
            ImGuiMCP::Text("%.1f", ms);
    }

    // Renders the save-load -> in-game timings captured by LoadProfiling.
    void RenderSaveLoadTimes() {
        const auto loads = LoadProfiling::Snapshot();
        if (!ImGuiMCP::CollapsingHeader("Save Load Times")) return;

        if (loads.empty()) {
            ImGuiMCP::TextDisabled("No save loaded yet this session.");
            return;
        }

        HelpMarker("(?)",
                   "Deserialize = SKSE kPreLoadGame -> kPostLoadGame (read + form load + globals).\n"
                   "  Pre-form    = file read + LoadMods (before the first change-form).\n"
                   "  Change-forms = the three change-form apply loops (per-mod breakdown below).\n"
                   "  Global-data = InitGlobalData -> FinishLoadGlobalData; INCLUDES cell/reference/3D\n"
                   "                load, so it is usually most of the load. Papyrus restore is within it.\n"
                   "  (Deserialize also has a small untimed tail = global-data + the residual.)\n"
                   "Papyrus = SkyrimVM script-state restore (a slice of Global-data).\n"
                   "Menu = Loading Menu visible (open -> close).\n"
                   "In-control = kPreLoadGame -> TESLoadGameEvent (fully loaded).\n"
                   "Trailing = kPostLoadGame -> Loading Menu close (world streaming after deserialize).");

        // No ScrollX: stretch all columns to fit so none scroll off-screen. Units are ms
        // (see the help tooltip); headers stay short to keep 11 columns readable.
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
                RenderLoadCell(load.deserializeMs);
                ImGuiMCP::TableSetColumnIndex(3);
                RenderLoadCell(load.preFormMs);
                ImGuiMCP::TableSetColumnIndex(4);
                RenderLoadCell(load.formSpanMs);
                ImGuiMCP::TableSetColumnIndex(5);
                RenderLoadCell(load.globalDataMs);
                ImGuiMCP::TableSetColumnIndex(6);
                RenderLoadCell(load.papyrusMs);
                ImGuiMCP::TableSetColumnIndex(7);
                RenderLoadCell(load.menuVisibleMs);
                ImGuiMCP::TableSetColumnIndex(8);
                RenderLoadCell(load.inControlMs);
                ImGuiMCP::TableSetColumnIndex(9);
                RenderLoadCell(load.postToCloseMs);
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
    void RenderPerDllLoadCost() {
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
            ImGuiMCP::TableSetupColumn("PreLoadGame (ms)");
            ImGuiMCP::TableSetupColumn("PostLoadGame (ms)");
            ImGuiMCP::TableSetupColumn("Total (ms)");
            ImGuiMCP::TableHeadersRow();
            for (const auto& row : rows) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", row.module.c_str());
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%.1f", row.preMs);
                ImGuiMCP::TableSetColumnIndex(2);
                ImGuiMCP::Text("%.1f", row.postMs);
                ImGuiMCP::TableSetColumnIndex(3);
                ImGuiMCP::Text("%.1f", row.preMs + row.postMs);
            }
            ImGuiMCP::EndTable();
        }
    }
}

namespace {
    // Per-mod change-form deserialize cost for the most recent load (ChangeFormProfiling).
    // Mirrors the "Change-forms by mod" export section.
    void RenderChangeFormsByMod() {
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
            ImGuiMCP::TableSetupColumn("Total (ms)");
            ImGuiMCP::TableHeadersRow();
            for (const auto& r : rows) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", r.plugin.c_str());
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%llu", static_cast<unsigned long long>(r.count));
                ImGuiMCP::TableSetColumnIndex(2);
                ImGuiMCP::Text("%.2f", r.totalMs);
            }
            ImGuiMCP::EndTable();
        }
    }

    // BSA (archive) vs loose-file asset reads for the most recent load (AssetReadProfiling).
    // Mirrors the "Asset reads" export section.
    void RenderAssetReads() {
        if (!ImGuiMCP::CollapsingHeader("Asset Reads: BSA vs Loose (last load)")) return;

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
            ImGuiMCP::TableSetupColumn("Time (ms)");
            ImGuiMCP::TableHeadersRow();
            const auto row = [](const char* label, const AssetReadProfiling::Stats& s) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::Text("%s", label);
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::Text("%llu", static_cast<unsigned long long>(s.calls));
                ImGuiMCP::TableSetColumnIndex(2);
                ImGuiMCP::Text("%.2f", static_cast<double>(s.bytes) / (1024.0 * 1024.0));
                ImGuiMCP::TableSetColumnIndex(3);
                ImGuiMCP::Text("%.1f", static_cast<double>(s.totalNs) / 1'000'000.0);
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
    RenderSaveLoadTimes();
    RenderChangeFormsByMod();
    RenderAssetReads();
    RenderPerDllLoadCost();
}