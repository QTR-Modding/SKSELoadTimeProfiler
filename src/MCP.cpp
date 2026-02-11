#include "MCP.h"
#include <Settings.h>
#include <Utils.h>
#include "MessagingProfilerUI.h"


void MCP::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::error("SKSEMenuFramework is not installed.");
        return;
    }
    SKSEMenuFramework::SetSection("Utilities");
    SKSEMenuFramework::AddSectionItem("Log", RenderLog);
    SKSEMenuFramework::AddSectionItem("Messaging Profiler", RenderProfiler);
}

void __stdcall MCP::RenderLog() {
    bool dirty = false;
    if (ImGuiMCP::ImGui::Checkbox("Trace", &LogSettings::log_trace)) dirty = true;
    ImGuiMCP::ImGui::SameLine();
    if (ImGuiMCP::ImGui::Checkbox("Info", &LogSettings::log_info)) dirty = true;
    ImGuiMCP::ImGui::SameLine();
    if (ImGuiMCP::ImGui::Checkbox("Warning", &LogSettings::log_warning)) dirty = true;
    ImGuiMCP::ImGui::SameLine();
    if (ImGuiMCP::ImGui::Checkbox("Error", &LogSettings::log_error)) dirty = true;
    if (dirty) {
        LogSettings::Save();
        dirty = false;
    }
    if (ImGuiMCP::ImGui::Button("Generate Log")) { logLines = Utilities::ReadLogFile(); }
    for (const auto& line : logLines) {
        if (line.find("trace") != std::string::npos && !LogSettings::log_trace) continue;
        if (line.find("info") != std::string::npos && !LogSettings::log_info) continue;
        if (line.find("warning") != std::string::npos && !LogSettings::log_warning) continue;
        if (line.find("error") != std::string::npos && !LogSettings::log_error) continue;
        ImGuiMCP::ImGui::Text("%s", line.c_str());
    }
}

void __stdcall MCP::RenderProfiler() {
    MessagingProfilerUI::State& state = MessagingProfilerUI::GetState();
    ImGuiMCP::ImGui::TextUnformatted("Messaging Callback Durations (ms) & ESP Load Times");
    ImGuiMCP::ImGui::Separator();
    bool thresholdsDirty = false;
    ImGuiMCP::ImGui::PushID("prof-thresholds");
    ImGuiMCP::ImGui::SetNextItemWidth(140);
    {
        float warn = static_cast<float>(profilerWarnMs);
        if (ImGuiMCP::ImGui::DragFloat("Warn (ms)", &warn, 10.f, 0.f, 10000.f, "%.0f")) {
            profilerWarnMs = warn;
            thresholdsDirty = true;
        }
    }
    ImGuiMCP::ImGui::SameLine();
    ImGuiMCP::ImGui::SetNextItemWidth(140);
    {
        float crit = static_cast<float>(profilerCritMs);
        if (ImGuiMCP::ImGui::DragFloat("Crit (ms)", &crit, 10.f, 0.f, 20000.f, "%.0f")) {
            profilerCritMs = crit;
            thresholdsDirty = true;
        }
    }
    ImGuiMCP::ImGui::PopID();
    if (ImGuiMCP::ImGui::Button("Save Settings")) LogSettings::Save();
    if (thresholdsDirty) LogSettings::Save();

    // New visibility filters for source kind
    ImGuiMCP::ImGui::Separator();
    ImGuiMCP::ImGui::TextUnformatted("Source Filters:");
    ImGuiMCP::ImGui::SameLine();
    bool dll = showDllEntries;
    if (ImGuiMCP::ImGui::Checkbox("DLL", &dll)) {
        showDllEntries = dll;
        LogSettings::Save();
    }
    ImGuiMCP::ImGui::SameLine();
    bool esp = showEspEntries;
    if (ImGuiMCP::ImGui::Checkbox("ESP", &esp)) {
        showEspEntries = esp;
        LogSettings::Save();
    }

    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs);
    ImGuiMCP::ImGui::Separator();
    ImGuiMCP::ImGui::TextWrapped(
        "Totals row sums visible per-module averages for selected message types. Threshold colors use warn/crit values. ESP rows only show total load time aggregated.");
}