#pragma once

// Backend accessors implemented in plugin.cpp
namespace MessagingProfilerBackend {
    std::vector<std::string_view> GetMessageTypeNames();
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations();
    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs();
}

namespace MessagingProfilerUI {
    struct State {
        std::vector<bool> selected; // matches GetMessageTypeNames size
        int sortColumn = 0; // 0 module, 1 total, >=2 message columns
        bool sortAsc = true;
        bool initializedFromDisk = false;
    };

    void EnsureSelectionSize(State& s, std::size_t count);
    void Render(State& s, double warnMs, double critMs);

    // Persistence helpers
    void SetInitialVisibility(const std::vector<bool>& vis);
    std::vector<bool> GetCurrentVisibility();
}