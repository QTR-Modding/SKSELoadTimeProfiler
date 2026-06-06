#include "MCP.h"
#include "LoadProfiling.h"
#include "MessagingProfilerUI.h"
#include "Localization.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

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
                   "Menu = Loading Menu visible (open -> close).\n"
                   "In-control = kPreLoadGame -> TESLoadGameEvent (fully loaded).\n"
                   "Trailing = kPostLoadGame -> Loading Menu close (world streaming after deserialize).");

        if (ImGuiMCP::ImGui::BeginTable("##loadtimes", 6,
                                        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders |
                                        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollX)) {
            ImGuiMCP::ImGui::TableSetupColumn("Save");
            ImGuiMCP::ImGui::TableSetupColumn("Deserialize (ms)");
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
                RenderLoadCell(load.deserializeMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(2);
                RenderLoadCell(load.menuVisibleMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(3);
                RenderLoadCell(load.inControlMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(4);
                RenderLoadCell(load.postToCloseMs);
                ImGuiMCP::ImGui::TableSetColumnIndex(5);
                if (load.success)
                    ImGuiMCP::ImGui::Text("ok");
                else
                    ImGuiMCP::ImGui::TextDisabled("FAILED");
            }
            ImGuiMCP::ImGui::EndTable();
        }
    }
}

void __stdcall MCP::RenderProfiler() {
    MessagingProfilerUI::State& state = MessagingProfilerUI::GetState();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs, showDllEntries, showEspEntries);
    ImGuiMCP::ImGui::Separator();
    RenderSaveLoadTimes();
}