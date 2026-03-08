#pragma once

void HelpMarker(const char* label, const char* desc);

namespace MCP {
    inline double profilerWarnMs = 800.0;
    inline double profilerCritMs = 2000.0;
    inline bool showDllEntries = true;
    inline bool showEspEntries = true;
    inline bool autoExportWithMenuFramework = false;
    inline std::atomic loadTimeMs{-1.0};

    void Register();
    void __stdcall RenderProfiler();
}