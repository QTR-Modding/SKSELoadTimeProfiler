#include "MessagingProfilerUI.h"
#include "MCP.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <unordered_map>
#include <windows.h>

namespace MessagingProfilerBackend {
    std::vector<std::string_view> GetMessageTypeNames();
    std::vector<std::pair<std::string, std::array<double, SKSE::MessagingInterface::kTotal>>> GetAverageDurations();
}

namespace MessagingProfilerUI {
    static State g_state; // global UI state for persistence

    void EnsureSelectionSize(State& s, const std::size_t count) {
        if (s.selected.size() != count) s.selected.assign(count, true);
    }

    static void ColorCell(const double v, const double warnMs, const double critMs) {
        ImU32 col = 0;
        if (v >= critMs) col = ImGui::GetColorU32(ImVec4(0.85f,0.15f,0.15f,0.50f));
        else if (v >= warnMs) col = ImGui::GetColorU32(ImVec4(0.95f,0.75f,0.10f,0.40f));
        if (col) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, col);
    }

    void SetInitialVisibility(const std::vector<bool>& vis) {
        g_state.selected = vis; g_state.initializedFromDisk = true;
    }

    std::vector<bool> GetCurrentVisibility() { return g_state.selected; }

    struct DllMeta { std::string author; std::string version; std::string license; bool ok=false; };
    static std::unordered_map<std::string, DllMeta> g_metaCache;

    static bool QueryVersionString(const wchar_t* path, const wchar_t* key, std::wstring& out)
    {
        DWORD handle = 0;
        DWORD sz = GetFileVersionInfoSizeW(path, &handle);
        if (!sz) return false;
        std::vector<char> buf(sz);
        if (!GetFileVersionInfoW(path, 0, sz, buf.data())) return false;
        struct LANGANDCODEPAGE { WORD wLanguage; WORD wCodePage; };
        LANGANDCODEPAGE* lpTranslate = nullptr; UINT cbTranslate = 0;
        if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate) || cbTranslate < sizeof(LANGANDCODEPAGE)) {
            // fallback to US English 0409/04B0
            static LANGANDCODEPAGE fallback{ 0x0409, 1200 };
            lpTranslate = &fallback; cbTranslate = sizeof(LANGANDCODEPAGE);
        }
        wchar_t query[256]{};
        for (UINT i = 0; i < cbTranslate/sizeof(LANGANDCODEPAGE); ++i) {
            _snwprintf_s(query, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage, key);
            LPVOID val = nullptr; UINT bytes = 0;
            if (VerQueryValueW(buf.data(), query, &val, &bytes) && val && bytes) {
                out.assign(static_cast<const wchar_t*>(val));
                return true;
            }
        }
        return false;
    }

    static std::string WideToUtf8(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        int len = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<std::size_t>(len - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
        return out;
    }

    static DllMeta GetDllMeta(const std::string& moduleBase)
    {
        DllMeta meta;
        std::string dllName = moduleBase + ".dll";
        HMODULE h = GetModuleHandleA(dllName.c_str());
        if (!h) return meta;
        wchar_t wpath[MAX_PATH]{};
        if (!GetModuleFileNameW(h, wpath, MAX_PATH)) return meta;
        std::wstring w;
        // Author
        if (QueryVersionString(wpath, L"CompanyName", w) || QueryVersionString(wpath, L"Author", w))
            meta.author = WideToUtf8(w);
        // Version
        if (QueryVersionString(wpath, L"ProductVersion", w) || QueryVersionString(wpath, L"FileVersion", w))
            meta.version = WideToUtf8(w);
        // License
        if (QueryVersionString(wpath, L"License", w) || QueryVersionString(wpath, L"LegalCopyright", w))
            meta.license = WideToUtf8(w);
        meta.ok = !(meta.author.empty() && meta.version.empty() && meta.license.empty());
        return meta;
    }

    void Render(State& s, const double warnMs, const double critMs) {
        auto names = MessagingProfilerBackend::GetMessageTypeNames();
        EnsureSelectionSize(s, names.size());
        auto rows = MessagingProfilerBackend::GetAverageDurations();

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

            // Compute displayed column totals from enriched rows
            std::vector<double> colTotals(active.size(), 0.0);
            for (auto& e : enriched) {
                for (std::size_t c=0; c<active.size(); ++c) {
                    double v = (*e.vals)[active[c]]; if (v < 1.0) v = 0.0; colTotals[c] += v;
                }
            }
            double totalsSum = 0.0; for (double v : colTotals) totalsSum += v;

            // Totals row based on displayed values
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("<Totals>");
            ImGui::TableSetColumnIndex(1); { ColorCell(totalsSum,warnMs,critMs); ImGui::Text("%.1f", totalsSum); }
            for (std::size_t c=0;c<active.size();++c){ ImGui::TableSetColumnIndex(static_cast<int>(c+2)); double v=colTotals[c]; ColorCell(v,warnMs,critMs); ImGui::Text("%.1f", v);}            

            // Module rows
            for (auto& e : enriched) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.module.c_str());
                if (ImGui::IsItemHovered()) {
                    auto it = g_metaCache.find(e.module);
                    if (it == g_metaCache.end()) it = g_metaCache.emplace(e.module, GetDllMeta(e.module)).first;
                    if (ImGui::BeginTooltip()) {
                        ImGui::TextUnformatted(e.module.c_str());
                        if (it->second.ok) {
                            if (!it->second.author.empty()) ImGui::Text("Author: %s", it->second.author.c_str());
                            if (!it->second.version.empty()) ImGui::Text("Version: %s", it->second.version.c_str());
                            if (!it->second.license.empty()) ImGui::Text("License: %s", it->second.license.c_str());
                        } else {
                            ImGui::TextUnformatted("(no version info)");
                        }
                        ImGui::EndTooltip();
                    }
                }
                ImGui::TableSetColumnIndex(1); ColorCell(e.total,warnMs,critMs); ImGui::Text("%.1f", e.total);
                for (std::size_t c=0;c<active.size();++c){ ImGui::TableSetColumnIndex(static_cast<int>(c+2)); double v=(*e.vals)[active[c]]; if(v<1.0) v=0.0; ColorCell(v,warnMs,critMs); ImGui::Text("%.1f", v);}            
            }
            ImGui::EndTable();
        }
    }
}
