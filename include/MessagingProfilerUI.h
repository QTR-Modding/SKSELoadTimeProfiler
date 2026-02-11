#pragma once

namespace MessagingProfilerUI {
    struct State {
        std::vector<bool> selected;
        int sortColumn = 0;
        bool sortAsc = true;
        bool initializedFromDisk = false;
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

    void Render(State& s, double warnMs, double critMs);

    void ColorCell(double v, double warnMs, double critMs);
}