#include "MCP.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <Settings.h>
#include <Utils.h>
#include "MessagingProfilerUI.h"
#include "ReferenceUI.h"
#include "ClibUtil/editorID.hpp"

TripletID::operator std::string_view() const noexcept { return unified_output; }
TripletID::operator std::string() const { return unified_output; }

TripletID::TripletID(const RE::TESForm* a_form) {
    if (!a_form) {
        unified_output = "None";
        return;
    }
    name = a_form->GetName();
    formID = a_form->GetFormID();
    editorID = clib_util::editorID::get_editorID(a_form);
    formtype = RE::FormTypeToString(a_form->GetFormType());
    unified_output = std::format("Name: {} | FormID: {:x} | EditorID: {} | FormType: {}", name, formID, editorID,
                                 formtype);
}

void TripletID::to_imgui() const {
    ImGui::PushID(this);
    MCP::UI::ReadOnlyField(nullptr, unified_output, "##triplet");
    ImGui::PopID();
}

void MCP::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::error("SKSEMenuFramework is not installed.");
        return;
    }
    SKSEMenuFramework::SetSection(Utilities::mod_name);
    SKSEMenuFramework::AddSectionItem("Reference", Reference::Render);
    SKSEMenuFramework::AddSectionItem("Log", RenderLog);
    SKSEMenuFramework::AddSectionItem("Messaging Profiler", RenderProfiler);
}

void __stdcall MCP::RenderLog() {
    bool dirty = false;
    if (ImGui::Checkbox("Trace", &LogSettings::log_trace)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Info", &LogSettings::log_info)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Warning", &LogSettings::log_warning)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Error", &LogSettings::log_error)) dirty = true;
    if (dirty) {
        LogSettings::Save();
        dirty = false;
    }
    if (ImGui::Button("Generate Log")) { logLines = Utilities::ReadLogFile(); }
    for (const auto& line : logLines) {
        if (line.find("trace") != std::string::npos && !LogSettings::log_trace) continue;
        if (line.find("info") != std::string::npos && !LogSettings::log_info) continue;
        if (line.find("warning") != std::string::npos && !LogSettings::log_warning) continue;
        if (line.find("error") != std::string::npos && !LogSettings::log_error) continue;
        ImGui::Text("%s", line.c_str());
    }
}

void __stdcall MCP::RenderProfiler() {
    static MessagingProfilerUI::State& state = *([]() { return new MessagingProfilerUI::State(); })();
    ImGui::TextUnformatted("Messaging Callback Durations (ms)");
    ImGui::Separator();
    bool thresholdsDirty = false;
    ImGui::PushID("prof-thresholds");
    ImGui::SetNextItemWidth(140);
    {
        float warn = static_cast<float>(profilerWarnMs);
        if (ImGui::DragFloat("Warn (ms)", &warn, 10.f, 0.f, 10000.f, "%.0f")) {
            profilerWarnMs = warn;
            thresholdsDirty = true;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    {
        float crit = static_cast<float>(profilerCritMs);
        if (ImGui::DragFloat("Crit (ms)", &crit, 10.f, 0.f, 20000.f, "%.0f")) {
            profilerCritMs = crit;
            thresholdsDirty = true;
        }
    }
    ImGui::PopID();
    if (ImGui::Button("Save Settings")) LogSettings::Save();
    if (thresholdsDirty) LogSettings::Save();
    MessagingProfilerUI::Render(state, profilerWarnMs, profilerCritMs);
    ImGui::Separator();
    ImGui::TextWrapped(
        "Totals row sums visible per-module averages for selected message types. Threshold colors use warn/crit values.");
}