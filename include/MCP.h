#pragma once
#include "Utils.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

static void HelpMarker(const char* desc);

namespace MCP::UI {
    inline void ReadOnlyField(const char* label, const std::string& value, const char* id) {
        if (label && *label) { ImGui::TextUnformatted(label); ImGui::SameLine(); }
        std::string buffer = value; ImGui::InputText(id, buffer.data(), buffer.size()+1, ImGuiInputTextFlags_ReadOnly);
    }
}

struct TripletID {
    explicit operator std::string_view() const noexcept;
    explicit operator std::string() const;
    explicit TripletID(const RE::TESForm* a_form = nullptr);
    void to_imgui() const;
private:
    std::string name; RE::FormID formID; std::string editorID; std::string formtype; std::string unified_output;
};

namespace MCP {
    inline std::string log_path = Utilities::GetLogPath().string();
    inline std::vector<std::string> logLines;
    inline double profilerWarnMs = 2.0; inline double profilerCritMs = 10.0;

    void Register();
    void __stdcall RenderLog();
    void __stdcall RenderProfiler();

    // Forward declarations for separated modules
    namespace Reference { void __stdcall Render(); }
}