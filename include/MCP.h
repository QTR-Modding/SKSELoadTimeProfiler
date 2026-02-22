#pragma once
#include "Utils.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

static void HelpMarker(const char* desc);

namespace MCP::UI {
    inline void ReadOnlyField(const char* label, const std::string& value, const char* id) {
        if (label && *label) {
            ImGuiMCP::ImGui::TextUnformatted(label);
            ImGuiMCP::ImGui::SameLine();
        }
        std::string buffer = value;
        ImGuiMCP::ImGui::InputText(id, buffer.data(), buffer.size() + 1, ImGuiMCP::ImGuiInputTextFlags_ReadOnly);
    }
}

namespace MCP {
    inline std::string log_path = Utilities::GetLogPath().string();
    inline std::vector<std::string> logLines;
    inline double profilerWarnMs = 800.0;
    inline double profilerCritMs = 2000.0;
    inline bool showDllEntries = true;
    inline bool showEspEntries = true;

    void Register();
    void __stdcall RenderLog();
    void __stdcall RenderProfiler();
}