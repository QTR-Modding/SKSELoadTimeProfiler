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
    ImGuiMCP::ImGui::TextDisabled("%s", label);
    if (ImGuiMCP::ImGui::IsItemHovered()) {
        ImGuiMCP::ImGui::BeginTooltip();
        ImGuiMCP::ImGui::TextUnformatted(desc);
        ImGuiMCP::ImGui::EndTooltip();
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
            ImGuiMCP::ImGui::TextDisabled("-");
        else
            ImGuiMCP::ImGui::Text("%.1f", ms);
    }

    // Renders the save-load -> in-game timings captured by LoadProfiling.
    void RenderSaveLoadTimes() {
        const auto loads = LoadProfiling::Snapshot();
        if (!ImGuiMCP::ImGui::CollapsingHeader("Save Load Times")) return;

        if (loads.empty()) {
            ImGuiMCP::ImGui::TextDisabled("No save loaded yet this session.");
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

        if (ImGuiMCP::ImGui::BeginTable("##loadtimes", 11,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollX)) {
            ImGuiMCP::ImGui::TableSetupColumn("Name");
            ImGuiMCP::ImGui::TableSetupColumn("Type");
            ImGuiMCP::ImGui::TableSetupColumn("Deserialize (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Pre-form (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Change-forms (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Global-data (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Papyrus (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Menu (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("In-control (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Trailing (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Result");
            ImGuiMCP::ImGui::TableHeadersRow();

            for (const auto& load : loads) {
                ImGuiMCP::ImGui::TableNextRow();
                ImGuiMCP::ImGui::TableSetColumnIndex(0);
                ImGuiMCP::ImGui::Text("%s", load.name.empty() ? "<unknown>" : load.name.c_str());
                ImGuiMCP::ImGui::TableSetColumnIndex(1);
                ImGuiMCP::ImGui::Text("%s", load.kind.c_str());
                ImGuiMCP::ImGui::TableSetColumnIndex(2);
                RenderLoadCell(load.deserializeMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(3);
                RenderLoadCell(load.preFormMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(4);
                RenderLoadCell(load.formSpanMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(5);
                RenderLoadCell(load.globalDataMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(6);
                RenderLoadCell(load.papyrusMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(7);
                RenderLoadCell(load.menuVisibleMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(8);
                RenderLoadCell(load.inControlMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(9);
                RenderLoadCell(load.postToCloseMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(10);
                if (load.success)
                    ImGuiMCP::ImGui::Text("ok");
                else
                    ImGuiMCP::ImGui::TextDisabled("FAILED");
            }
            ImGuiMCP::ImGui::EndTable();
        }
    }
}

namespace {
    // Per-DLL save-load cost: each plugin's time in its kPreLoadGame + kPostLoadGame
    // handlers (the startup table filters out "...Game" messages, so they surface here).
    void RenderPerDllLoadCost() {
        using MI = SKSE::MessagingInterface;
        if (!ImGuiMCP::ImGui::CollapsingHeader("Per-DLL Load-Game Cost")) return;

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
            ImGuiMCP::ImGui::TextDisabled("No DLL load-game work recorded yet.");
            return;
        }

        if (ImGuiMCP::ImGui::BeginTable("##dllloadcost", 4,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollY)) {
            ImGuiMCP::ImGui::TableSetupColumn("DLL");
            ImGuiMCP::ImGui::TableSetupColumn("PreLoadGame (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("PostLoadGame (ms)");
            ImGuiMCP::ImGui::TableSetupColumn("Total (ms)");
            ImGuiMCP::ImGui::TableHeadersRow();
            for (const auto& row : rows) {
                ImGuiMCP::ImGui::TableNextRow();
                ImGuiMCP::ImGui::TableSetColumnIndex(0);
                ImGuiMCP::ImGui::Text("%s", row.module.c_str());
                ImGuiMCP::ImGui::TableSetColumnIndex(1);
                ImGuiMCP::ImGui::Text("%.1f", row.preMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(2);
                ImGuiMCP::ImGui::Text("%.1f", row.postMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(3);
                ImGuiMCP::ImGui::Text("%.1f", row.preMs + row.postMs);
            }
            ImGuiMCP::ImGui::EndTable();
        }
    }
}

namespace {
    // Per-mod change-form deserialize cost for the most recent load (ChangeFormProfiling).
    // Mirrors the "Change-forms by mod" export section.
    void RenderChangeFormsByMod() {
        if (!ImGuiMCP::ImGui::CollapsingHeader("Change-forms by Mod (last load)")) return;

        HelpMarker("(?)",
                   "Per-mod cost of the change-form apply loops in the most recent save load.\n"
                   "Each change-form is keyed by its FormID's load-order byte and timed by the\n"
                   "wall-clock between consecutive iterations (lookup + Revert/apply). High totals\n"
                   "here mean that mod is bloating the save. This is the breakdown of the\n"
                   "'Change-forms' deserialize sub-phase above.");

        const auto rows = ChangeFormProfiling::SnapshotLast();
        if (rows.empty()) {
            ImGuiMCP::ImGui::TextDisabled("No change-form data captured yet this session.");
            return;
        }

        if (ImGuiMCP::ImGui::BeginTable("##changeforms", 3,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollY)) {
            ImGuiMCP::ImGui::TableSetupColumn("Plugin");
            ImGuiMCP::ImGui::TableSetupColumn("Forms");
            ImGuiMCP::ImGui::TableSetupColumn("Total (ms)");
            ImGuiMCP::ImGui::TableHeadersRow();
            for (const auto& r : rows) {
                ImGuiMCP::ImGui::TableNextRow();
                ImGuiMCP::ImGui::TableSetColumnIndex(0);
                ImGuiMCP::ImGui::Text("%s", r.plugin.c_str());
                ImGuiMCP::ImGui::TableSetColumnIndex(1);
                ImGuiMCP::ImGui::Text("%llu", static_cast<unsigned long long>(r.count));
                ImGuiMCP::ImGui::TableSetColumnIndex(2);
                ImGuiMCP::ImGui::Text("%.2f", r.totalMs);
            }
            ImGuiMCP::ImGui::EndTable();
        }
    }

    // BSA (archive) vs loose-file asset reads for the most recent load (AssetReadProfiling).
    // Mirrors the "Asset reads" export section.
    void RenderAssetReads() {
        if (!ImGuiMCP::ImGui::CollapsingHeader("Asset Reads: BSA vs Loose (last load)")) return;

        HelpMarker("(?)",
                   "Bytes and time read from BSA archives vs raw loose files during the last load.\n"
                   "Loose-file reads bypass the BSA cache and are typically far slower per byte;\n"
                   "a high loose share points at un-packed mod assets as a load-time cost.");

        const auto bsa = AssetReadProfiling::SnapshotArchive();
        const auto loose = AssetReadProfiling::SnapshotLoose();
        if (!bsa.calls && !loose.calls) {
            ImGuiMCP::ImGui::TextDisabled("No asset reads captured yet this session.");
            return;
        }

        if (ImGuiMCP::ImGui::BeginTable("##assetreads", 4,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable)) {
            ImGuiMCP::ImGui::TableSetupColumn("Source");
            ImGuiMCP::ImGui::TableSetupColumn("Reads");
            ImGuiMCP::ImGui::TableSetupColumn("MB");
            ImGuiMCP::ImGui::TableSetupColumn("Time (ms)");
            ImGuiMCP::ImGui::TableHeadersRow();
            const auto row = [](const char* label, const AssetReadProfiling::Stats& s) {
                ImGuiMCP::ImGui::TableNextRow();
                ImGuiMCP::ImGui::TableSetColumnIndex(0);
                ImGuiMCP::ImGui::Text("%s", label);
                ImGuiMCP::ImGui::TableSetColumnIndex(1);
                ImGuiMCP::ImGui::Text("%llu", static_cast<unsigned long long>(s.calls));
                ImGuiMCP::ImGui::TableSetColumnIndex(2);
                ImGuiMCP::ImGui::Text("%.2f", static_cast<double>(s.bytes) / (1024.0 * 1024.0));
                ImGuiMCP::ImGui::TableSetColumnIndex(3);
                ImGuiMCP::ImGui::Text("%.1f", static_cast<double>(s.totalNs) / 1'000'000.0);
            };
            row("BSA", bsa);
            row("Loose", loose);
            ImGuiMCP::ImGui::EndTable();
        }
    }
}

void __stdcall MCP::RenderProfiler() {
    MessagingProfilerUI::State& state = MessagingProfilerUI::GetState();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs, showDllEntries, showEspEntries);
    ImGuiMCP::ImGui::Separator();
    RenderSaveLoadTimes();
    RenderChangeFormsByMod();
    RenderAssetReads();
    RenderPerDllLoadCost();
}