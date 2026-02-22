#pragma once
#include "MCP.h"

namespace LogSettings {
    inline bool log_trace = true;
    inline bool log_info = true;
    inline bool log_warning = true;
    inline bool log_error = true;

    std::filesystem::path GetConfigPath();

    void Load();

    void Save();
};