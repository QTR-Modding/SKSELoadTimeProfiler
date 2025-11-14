#include "MCP.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <Settings.h>
#include <Utils.h>
#include "SKSE/Interfaces.h"
#include "MessagingProfilerUI.h"
#include "ReferenceUI.h" // newly added

#undef GetObject

TripletID::operator std::string_view() const noexcept { return unified_output; }
TripletID::operator std::string() const { return unified_output; }
TripletID::TripletID(const RE::TESForm *a_form) { if (!a_form){ unified_output="None"; return;} name=a_form->GetName(); formID=a_form->GetFormID(); editorID=clib_util::editorID::get_editorID(a_form); formtype=RE::FormTypeToString(a_form->GetFormType()); unified_output = std::format("Name: {} | FormID: {:x} | EditorID: {} | FormType: {}", name, formID, editorID, formtype); }
void TripletID::to_imgui() const { ImGui::PushID(this); MCP::UI::ReadOnlyField(nullptr, unified_output, "##triplet"); ImGui::PopID(); }

void MCP::Register() {
    if (!SKSEMenuFramework::IsInstalled()) { logger::error("SKSEMenuFramework is not installed."); return; }
    SKSEMenuFramework::SetSection(Utilities::mod_name);
    SKSEMenuFramework::AddSectionItem("Reference", Reference::Render);
    SKSEMenuFramework::AddSectionItem("Log", RenderLog);
    SKSEMenuFramework::AddSectionItem("Messaging Profiler", RenderProfiler);
}

void __stdcall MCP::RenderLog() {
    ImGui::Checkbox("Trace", &LogSettings::log_trace); ImGui::SameLine();
    ImGui::Checkbox("Info", &LogSettings::log_info); ImGui::SameLine();
    ImGui::Checkbox("Warning", &LogSettings::log_warning); ImGui::SameLine();
    if (ImGui::Button("Generate Log")) { logLines = Utilities::ReadLogFile(); }
    for (const auto &line : logLines) {
        if (line.find("trace")!=std::string::npos && !LogSettings::log_trace) continue;
        if (line.find("info")!=std::string::npos && !LogSettings::log_info) continue;
        if (line.find("warning")!=std::string::npos && !LogSettings::log_warning) continue;
        if (line.find("error")!=std::string::npos && !LogSettings::log_error) continue;
        ImGui::Text("%s", line.c_str());
    }
}

void __stdcall MCP::RenderProfiler() {
    static MessagingProfilerUI::State state;
    ImGui::TextUnformatted("Messaging Callback Average Durations (ms)");
    ImGui::Separator();
    ImGui::PushID("prof-thresholds");
    ImGui::SetNextItemWidth(120); { float warn = static_cast<float>(profilerWarnMs); if (ImGui::DragFloat("Warn (ms)", &warn, 0.1f, 0.0f, 1000.0f, "%.1f")) profilerWarnMs = warn; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120); { float crit = static_cast<float>(profilerCritMs); if (ImGui::DragFloat("Crit (ms)", &crit, 0.1f, 0.0f, 1000.0f, "%.1f")) profilerCritMs = crit; }
    ImGui::PopID();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs);
    ImGui::Separator();
    ImGui::TextWrapped("Select which message types to display. Total column sums displayed message type averages. Colors use thresholds.");
}
