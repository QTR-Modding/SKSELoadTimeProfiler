#pragma once
#include "MCP.h"

namespace LogSettings {
    inline bool log_trace = true;
    inline bool log_info = true;
    inline bool log_warning = true;
    inline bool log_error = true;
    inline uint64_t profiler_dispatch_warn_ns = 500'000;

    std::filesystem::path GetConfigPath();

    void Load();

    void Save();
};