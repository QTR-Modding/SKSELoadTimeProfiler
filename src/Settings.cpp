#include "Settings.h"
#include "MessagingProfiler.h"
#include "MessagingProfilerUI.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

std::filesystem::path Settings::GetConfigPath() {
    const std::filesystem::path path = R"(Data/SKSE/Plugins/LoadTimeProfiler/LoadTimeProfiler.json)";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path;
}

void Settings::Load() {
    auto path = GetConfigPath();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return; // no file -> defaults (UI will initialize all true)
    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document doc;
    doc.ParseStream(isw);
    if (doc.HasParseError() || !doc.IsObject()) return;
    if (doc.HasMember("profiler_warn_ms") && doc["profiler_warn_ms"].IsNumber())
        MCP::profilerWarnMs = doc["profiler_warn_ms"].GetDouble();
    if (doc.HasMember("profiler_crit_ms") && doc["profiler_crit_ms"].IsNumber())
        MCP::profilerCritMs = doc["profiler_crit_ms"].GetDouble();
    if (doc.HasMember("show_dll_entries") && doc["show_dll_entries"].IsBool())
        MCP::showDllEntries = doc["show_dll_entries"].GetBool();
    if (doc.HasMember("show_esp_entries") && doc["show_esp_entries"].IsBool())
        MCP::showEspEntries = doc["show_esp_entries"].GetBool();
    if (doc.HasMember("export") && doc["export"].IsBool())
        MCP::autoExportWithMenuFramework = doc["export"].GetBool();
    if (doc.HasMember("show_seconds") && doc["show_seconds"].IsBool())
        MessagingProfilerUI::GetState().showSeconds = doc["show_seconds"].GetBool();
    if (doc.HasMember("profiler_visible") && doc["profiler_visible"].IsArray()) {
        auto arr = doc["profiler_visible"].GetArray();
        auto names = MessagingProfiler::GetMessageTypeNames();
        std::vector vis(names.size(), false);
        for (auto& v : arr)
            if (v.IsString()) {
                std::string s = v.GetString();
                for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == s) vis[i] = true;
            }
        // If user never selected anything (empty or all false), treat as default: all true
        bool anySelected = false;
        for (bool b : vis) {
            if (b) {
                anySelected = true;
                break;
            }
        }
        if (!anySelected) std::ranges::fill(vis, true);
        MessagingProfilerUI::SetInitialVisibility(vis);
    }
}

void Settings::Save() {
    rapidjson::Document doc(rapidjson::kObjectType);
    auto& a = doc.GetAllocator();
    doc.AddMember("profiler_warn_ms", MCP::profilerWarnMs, a);
    doc.AddMember("profiler_crit_ms", MCP::profilerCritMs, a);
    doc.AddMember("show_dll_entries", MCP::showDllEntries, a);
    doc.AddMember("show_esp_entries", MCP::showEspEntries, a);
    doc.AddMember("export", MCP::autoExportWithMenuFramework, a);
    doc.AddMember("show_seconds", MessagingProfilerUI::GetState().showSeconds, a);
    auto names = MessagingProfiler::GetMessageTypeNames();
    auto vis = MessagingProfilerUI::GetCurrentVisibility();
    rapidjson::Value arr(rapidjson::kArrayType);
    for (std::size_t i = 0; i < names.size() && i < vis.size(); ++i) {
        if (vis[i]) {
            arr.PushBack(
                rapidjson::Value(names[i].data(), static_cast<rapidjson::SizeType>(names[i].size()), a), a);
        }
    }
    doc.AddMember("profiler_visible", arr, a);
    rapidjson::StringBuffer sb;
    rapidjson::Writer wr(sb);
    doc.Accept(wr);
    auto path = GetConfigPath();
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return;
    ofs << sb.GetString();
}