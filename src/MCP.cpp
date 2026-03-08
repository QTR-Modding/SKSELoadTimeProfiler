#include "MCP.h"
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

void __stdcall MCP::RenderProfiler() {
    MessagingProfilerUI::State& state = MessagingProfilerUI::GetState();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs, showDllEntries, showEspEntries);
}