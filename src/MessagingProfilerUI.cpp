#include "MessagingProfilerUI.h"
#include "MCP.h"
#include <SKSEMCP/SKSEMenuFramework.hpp> // brings in ImGui
#include <algorithm>

namespace MessagingProfilerBackend {
    std::vector<std::string_view> GetMessageTypeNames();
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations();
    std::array<double, SKSE::MessagingInterface::kTotal> GetTotalsAvgMs();
}

namespace MessagingProfilerUI {
    void EnsureSelectionSize(State& s, std::size_t count) {
        if (s.selected.size() != count) s.selected.assign(count, true);
    }

    static inline void ColorCell(double v, double warnMs, double critMs) {
        ImU32 col = 0;
        if (v >= critMs) col = ImGui::GetColorU32(ImVec4(0.85f,0.15f,0.15f,0.50f));
        else if (v >= warnMs) col = ImGui::GetColorU32(ImVec4(0.95f,0.75f,0.10f,0.40f));
        if (col) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, col);
    }

    void Render(State& s, double warnMs, double critMs) {
        auto names = MessagingProfilerBackend::GetMessageTypeNames();
        EnsureSelectionSize(s, names.size());
        auto rows = MessagingProfilerBackend::GetAverageDurations();
        auto totals = MessagingProfilerBackend::GetTotalsAvgMs();

        if (ImGui::CollapsingHeader("Message Types")) {
            ImGui::Indent();
            for (std::size_t i=0;i<names.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                bool sel = s.selected[i];
                if (ImGui::Checkbox(names[i].data(), &sel)) s.selected[i] = sel;
                ImGui::PopID();
                if ((i % 4)!=3) ImGui::SameLine();
            }
            if (ImGui::Button("All")) std::fill(s.selected.begin(), s.selected.end(), true);
            ImGui::SameLine();
            if (ImGui::Button("None")) std::fill(s.selected.begin(), s.selected.end(), false);
            ImGui::Unindent();
        }

        std::vector<std::size_t> active;
        for (std::size_t i=0;i<s.selected.size();++i) if (s.selected[i]) active.push_back(i);
        if (active.empty()) { ImGui::TextUnformatted("No message types selected."); return; }

        if (ImGui::BeginTable("##msgprof2", static_cast<int>(active.size()) + 2, ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_Resizable|ImGuiTableFlags_Reorderable|ImGuiTableFlags_Sortable|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_DefaultSort|ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_PreferSortDescending);
            for (auto idx : active) ImGui::TableSetupColumn(names[idx].data(), ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsCount>0) {
                    s.sortColumn = sortSpecs->Specs[0].ColumnIndex;
                    s.sortAsc = sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                }
            }

            struct RowWrap { std::string module; double total; const std::array<double, SKSE::MessagingInterface::kTotal>* vals; };
            std::vector<RowWrap> enriched; enriched.reserve(rows.size());
            for (auto& r : rows) {
                double sum=0.0; for (auto idx:active){ double v=r.second[idx]; if(v<1.0) v=0.0; sum+=v; }
                enriched.push_back({r.first,sum,&r.second});
            }

            std::sort(enriched.begin(), enriched.end(), [&](const RowWrap& A, const RowWrap& B){
                if (s.sortColumn==0) return s.sortAsc ? A.module < B.module : A.module > B.module;
                if (s.sortColumn==1) return s.sortAsc ? A.total < B.total : A.total > B.total;
                std::size_t msgIdx = active[s.sortColumn-2];
                double av = (*A.vals)[msgIdx]; if (av<1.0) av=0.0;
                double bv = (*B.vals)[msgIdx]; if (bv<1.0) bv=0.0;
                return s.sortAsc ? av < bv : av > bv;
            });

            // Totals row
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("<Totals>");
            ImGui::TableSetColumnIndex(1); {
                double sum=0.0; for(auto idx:active){ double v=totals[idx]; if(v<1.0) v=0.0; sum+=v;} ColorCell(sum,warnMs,critMs); ImGui::Text("%.1f", sum);
            }
            for (std::size_t c=0;c<active.size();++c){ ImGui::TableSetColumnIndex(static_cast<int>(c+2)); double v=totals[active[c]]; if(v<1.0) v=0.0; ColorCell(v,warnMs,critMs); ImGui::Text("%.1f", v);}            

            // Module rows
            for (auto& e : enriched) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.module.c_str());
                ImGui::TableSetColumnIndex(1); ColorCell(e.total,warnMs,critMs); ImGui::Text("%.1f", e.total);
                for (std::size_t c=0;c<active.size();++c){ ImGui::TableSetColumnIndex(static_cast<int>(c+2)); double v=(*e.vals)[active[c]]; if(v<1.0) v=0.0; ColorCell(v,warnMs,critMs); ImGui::Text("%.1f", v);}            
            }
            ImGui::EndTable();
        }
    }
}
