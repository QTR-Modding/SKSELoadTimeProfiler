#pragma once

#include "rapidjson/document.h"
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

//#include <chrono>
#include <windows.h>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include "SimpleIni.h"
#include <iostream>
#include <string>
#include <codecvt>
#include <mutex>
#include <algorithm>
#include <ClibUtil/editorID.hpp>
#include <filesystem>
#include <fstream>

namespace Utilities {

    const auto mod_name = static_cast<std::string>(SKSE::PluginDeclaration::GetSingleton()->GetName());
    const auto plugin_version = SKSE::PluginDeclaration::GetSingleton()->GetVersion();

	std::filesystem::path GetLogPath();

	std::vector<std::string> ReadLogFile();

    std::optional<std::uint32_t> hex_to_u32(std::string_view s);
};