#pragma once

namespace MessagingProfilerUI {
    inline constexpr float MinTableHeightPercent = 10.0f;
    inline constexpr float MaxTableHeightPercent = 90.0f;
    inline constexpr float FullHeightPercent = 100.0f;

    struct State {
        std::vector<bool> selected;
        int sortColumn = 0;
        bool sortAsc = true;
        bool initializedFromDisk = false;
        bool showSeconds = true;
        float tableHeightPercent = 50.0f;
        int exportFormat = 0;
        std::string exportStatus;
        std::array<char, 96> search{};
    };

    struct DllMeta {
        std::string author;
        std::string version;
        std::string license;
        bool ok = false;
    };

    inline State g_state;
    // Access global persistent state
    inline State& GetState() { return g_state; }

    inline std::unordered_map<std::string, DllMeta> g_metaCache;

    inline void EnsureSelectionSize(State& s, const std::size_t count) {
        if (s.selected.size() != count) s.selected.assign(count, true);
    }

    inline void SetInitialVisibility(const std::vector<bool>& vis) {
        g_state.selected = vis;
        g_state.initializedFromDisk = true;
    }

    inline std::vector<bool> GetCurrentVisibility() { return g_state.selected; }

    bool QueryVersionString(const wchar_t* path, const wchar_t* key, std::wstring& out);

    DllMeta GetDllMeta(const std::string& moduleBase);

    void Render(State& s, double& warnMs, double& critMs, bool& showDllEntries, bool& showEspEntries);

    void ColorCell(double v, double warnMs, double critMs);
}
