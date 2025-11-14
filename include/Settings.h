#pragma once
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "MCP.h"
#include "MessagingProfilerUI.h"

namespace LogSettings {
    inline bool log_trace = true;
    inline bool log_info = true;
    inline bool log_warning = true;
    inline bool log_error = true;

    inline std::filesystem::path GetConfigPath() {
        const auto plugin = SKSE::PluginDeclaration::GetSingleton()->GetName();
        std::filesystem::path dllPath{REL::Module::get().filename()};
        auto dir = dllPath.parent_path() / std::filesystem::path(plugin);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / std::format("{}.json", plugin);
    }

    inline void Load() {
        auto path = GetConfigPath();
        std::ifstream ifs(path);
        if (!ifs.is_open()) return;
        rapidjson::IStreamWrapper isw(ifs);
        rapidjson::Document doc;
        doc.ParseStream(isw);
        if (doc.HasParseError() || !doc.IsObject()) return;
        if (doc.HasMember("profiler_warn_ms") && doc["profiler_warn_ms"].IsNumber())
            MCP::profilerWarnMs = doc["profiler_warn_ms"].GetDouble();
        if (doc.HasMember("profiler_crit_ms") && doc["profiler_crit_ms"].IsNumber())
            MCP::profilerCritMs = doc["profiler_crit_ms"].GetDouble();
        if (doc.HasMember("profiler_visible") && doc["profiler_visible"].IsArray()) {
            auto arr = doc["profiler_visible"].GetArray();
            auto names = MessagingProfilerBackend::GetMessageTypeNames();
            std::vector<bool> vis(names.size(), false);
            for (auto& v : arr)
                if (v.IsString()) {
                    std::string s = v.GetString();
                    for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == s) vis[i] = true;
                }
            MessagingProfilerUI::SetInitialVisibility(vis);
        }
    }

    inline void Save() {
        rapidjson::Document doc(rapidjson::kObjectType);
        auto& a = doc.GetAllocator();
        doc.AddMember("profiler_warn_ms", MCP::profilerWarnMs, a);
        doc.AddMember("profiler_crit_ms", MCP::profilerCritMs, a);
        auto names = MessagingProfilerBackend::GetMessageTypeNames();
        auto vis = MessagingProfilerUI::GetCurrentVisibility();
        rapidjson::Value arr(rapidjson::kArrayType);
        for (std::size_t i = 0; i < names.size() && i < vis.size(); ++i) if (vis[i]) arr.PushBack(
            rapidjson::Value(names[i].data(), a), a);
        doc.AddMember("profiler_visible", arr, a);
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
        doc.Accept(wr);
        auto path = GetConfigPath();
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return;
        ofs << sb.GetString();
    }
};