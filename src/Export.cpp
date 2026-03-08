#include "Export.h"
#include "ESPProfiling.h"
#include "Localization.h"
#include "MessagingProfiler.h"
#include "MessagingProfilerUI.h"
#include "Settings.h"
#include <fmt/printf.h>

namespace {
    struct SummaryMetrics {
        double skseInitMs{-1.0};
        double totalDllMs{0.0};
        double totalEspMs{0.0};
        double totalAllMs{0.0};
        std::size_t dllCount{0};
        std::size_t espCount{0};
    };

    struct ExportRow {
        std::string module;
        std::string author;
        std::string version;
        bool isEsp{false};
        double totalMs{0.0};
        std::array<double, SKSE::MessagingInterface::kTotal> perMsg{};
    };

    struct EspMeta {
        std::string author;
        double version{-1.0};
    };

    struct SystemInfo {
        std::string runtimeVariant;
        std::string runtimeVersion;
        std::string osNameVersion;
        std::string cpuVendor;
        std::string cpuModel;
        std::string gpuList;
    };

    struct ExportTotals {
        std::array<double, SKSE::MessagingInterface::kTotal> colTotals{};
        double espExtra{0.0};
        double grandTotal{0.0};
        std::size_t rowCount{0};
    };

    const char* PlaceholderText() {
        return Localization::PlaceholderEmpty.empty() ? "-" : Localization::PlaceholderEmpty.c_str();
    }

    std::string FormatSeconds(const double valueMs) {
        if (valueMs < 0.0) return {};
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << (valueMs / 1000.0);
        return os.str();
    }

    std::string FormatVersion(const double value) {
        if (value < 0.0) return PlaceholderText();
        std::ostringstream os;
        os << std::fixed << std::setprecision(3) << value;
        return os.str();
    }

    std::string EscapeCsv(std::string_view value) {
        bool needsQuote = false;
        for (const char c : value) {
            if (c == ',' || c == '"' || c == '\n' || c == '\r') {
                needsQuote = true;
                break;
            }
        }
        if (!needsQuote) return std::string(value);

        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('"');
        for (const char c : value) {
            if (c == '"') out.push_back('"');
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    std::string SanitizeText(std::string value) {
        for (char& c : value) {
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        }
        return value;
    }

    std::string Ellipsize(const std::string_view value, const std::size_t maxWidth) {
        std::string s = SanitizeText(std::string(value));
        if (s.size() <= maxWidth) return s;
        if (maxWidth <= 3) return s.substr(0, maxWidth);
        return s.substr(0, maxWidth - 3) + "...";
    }

    std::string MakeTimestampForFile() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        if (localtime_s(&tm, &nowTime) != 0) return "19700101_000000";

        std::ostringstream os;
        os << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return os.str();
    }

    std::filesystem::path BuildExportPath(const Export::Format format) {
        auto outDir = Settings::GetConfigPath().parent_path();
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        const char* ext = format == Export::Format::Txt ? ".txt" : ".csv";
        return outDir / ("LTP_summary_" + MakeTimestampForFile() + ext);
    }

    std::string TrimAscii(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string JoinStrings(const std::vector<std::string>& parts, const std::string_view separator) {
        if (parts.empty()) return {};
        std::string out = parts.front();
        for (std::size_t i = 1; i < parts.size(); ++i) {
            out += separator;
            out += parts[i];
        }
        return out;
    }

    std::string DetectWindowsName(const DWORD major, const DWORD minor, const DWORD build) {
        if (major == 10) return build >= 22000 ? "Windows 11" : "Windows 10";
        if (major == 6 && minor == 3) return "Windows 8.1";
        if (major == 6 && minor == 2) return "Windows 8";
        if (major == 6 && minor == 1) return "Windows 7";
        if (major == 6 && minor == 0) return "Windows Vista";
        if (major == 5 && minor == 1) return "Windows XP";
        return "Windows";
    }

    std::string GetWindowsVersionString() {
        DWORD major = 0;
        DWORD minor = 0;
        DWORD build = 0;

        if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
            using Fn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
            if (const auto fn = reinterpret_cast<Fn>(GetProcAddress(ntdll, "RtlGetNtVersionNumbers"))) {
                fn(&major, &minor, &build);
                build &= 0xFFFF;
            }
        }

        if (major == 0 && minor == 0 && build == 0) return PlaceholderText();
        return fmt::format("{} v{}.{}.{}", DetectWindowsName(major, minor, build), major, minor, build);
    }

    std::string GetSkyrimRuntimeVersionString() {
        const auto version = REL::Module::get().version();
        return fmt::format("{}.{}.{}", version[0], version[1], version[2]);
    }

    std::string GetCpuVendorString() {
        int regs[4]{};
        __cpuid(regs, 0);

        char vendor[13]{};
        std::memcpy(vendor + 0, &regs[1], sizeof(int));
        std::memcpy(vendor + 4, &regs[3], sizeof(int));
        std::memcpy(vendor + 8, &regs[2], sizeof(int));
        vendor[12] = '\0';

        const auto result = TrimAscii(vendor);
        return result.empty() ? PlaceholderText() : result;
    }

    std::string GetCpuModelString() {
        int regs[4]{};
        constexpr int kExtBaseLeaf = static_cast<int>(0x80000000u);
        __cpuid(regs, kExtBaseLeaf);
        const auto maxLeaf = static_cast<unsigned>(regs[0]);
        if (maxLeaf < 0x80000004) return PlaceholderText();

        std::array<char, 49> brand{};
        for (unsigned i = 0; i < 3; ++i) {
            __cpuid(regs, static_cast<int>(0x80000002u + i));
            std::memcpy(brand.data() + i * 16, regs, 16);
        }
        brand.back() = '\0';

        const auto model = TrimAscii(brand.data());
        return model.empty() ? PlaceholderText() : model;
    }

    std::string DetectGpuVendor(std::string deviceId) {
        std::transform(deviceId.begin(), deviceId.end(), deviceId.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (deviceId.find("VEN_10DE") != std::string::npos) return "Nvidia";
        if (deviceId.find("VEN_8086") != std::string::npos) return "Intel";
        if (deviceId.find("VEN_1002") != std::string::npos || deviceId.find("VEN_1022") != std::string::npos)
            return "AMD";
        return "Unknown";
    }

    std::string GetGpuListString() {
        std::vector<std::string> gpus;
        DISPLAY_DEVICEA adapter{};
        adapter.cb = sizeof(adapter);

        for (DWORD index = 0; EnumDisplayDevicesA(nullptr, index, &adapter, 0) != FALSE; ++index) {
            if ((adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) {
                adapter = {};
                adapter.cb = sizeof(adapter);
                continue;
            }

            auto model = TrimAscii(adapter.DeviceString);
            if (model.empty()) model = PlaceholderText();

            const auto vendor = DetectGpuVendor(adapter.DeviceID);
            const auto label = vendor == "Unknown" ? model : fmt::format("{} {}", vendor, model);
            if (std::ranges::find(gpus, label) == gpus.end()) {
                gpus.push_back(label);
            }

            adapter = {};
            adapter.cb = sizeof(adapter);
        }

        if (gpus.empty()) return PlaceholderText();
        return JoinStrings(gpus, "; ");
    }

    SystemInfo BuildSystemInfo() {
        SystemInfo info;
        info.runtimeVariant = REL::Module::IsVR() ? "VR" : "SSE";
        info.runtimeVersion = GetSkyrimRuntimeVersionString();
        info.osNameVersion = GetWindowsVersionString();
        info.cpuVendor = GetCpuVendorString();
        info.cpuModel = GetCpuModelString();
        info.gpuList = GetGpuListString();
        return info;
    }

    SummaryMetrics BuildSummaryMetrics(const std::vector<MessagingProfiler::TaggedRow>& taggedRows) {
        SummaryMetrics metrics;
        for (const auto& row : taggedRows) {
            if (row.kind == MessagingProfiler::SourceKind::ESP) {
                ++metrics.espCount;
                metrics.totalEspMs += row.totalMs;
            } else {
                ++metrics.dllCount;
                for (const double value : row.perMsg)
                    if (value >= 1.0) metrics.totalDllMs += value;
            }
        }

        metrics.skseInitMs = MCP::loadTimeMs.load(std::memory_order_relaxed);
        if (metrics.skseInitMs >= 0.0) metrics.totalDllMs += metrics.skseInitMs;
        metrics.totalAllMs = metrics.totalDllMs + metrics.totalEspMs;
        return metrics;
    }

    std::unordered_map<std::string, EspMeta> BuildEspMetaMap() {
        std::unordered_map<std::string, EspMeta> out;
        for (const auto& entry : ESPProfiling::SnapshotEntries()) {
            out[entry.name] = EspMeta{.author = entry.author, .version = entry.version};
        }
        return out;
    }

    std::string LookupDllAuthor(const std::string& module) {
        auto it = MessagingProfilerUI::g_metaCache.find(module);
        if (it == MessagingProfilerUI::g_metaCache.end())
            it = MessagingProfilerUI::g_metaCache.emplace(module, MessagingProfilerUI::GetDllMeta(module)).first;
        return it->second.author.empty() ? PlaceholderText() : it->second.author;
    }

    std::string LookupDllVersion(const std::string& module) {
        auto it = MessagingProfilerUI::g_metaCache.find(module);
        if (it == MessagingProfilerUI::g_metaCache.end())
            it = MessagingProfilerUI::g_metaCache.emplace(module, MessagingProfilerUI::GetDllMeta(module)).first;
        return it->second.version.empty() ? PlaceholderText() : it->second.version;
    }

    std::vector<ExportRow> BuildExportRows(const std::vector<MessagingProfiler::TaggedRow>& taggedRows) {
        std::vector<ExportRow> rows;
        rows.reserve(taggedRows.size());
        const auto espMeta = BuildEspMetaMap();

        for (const auto& row : taggedRows) {
            ExportRow out;
            out.module = row.module;
            out.isEsp = row.kind == MessagingProfiler::SourceKind::ESP;
            out.totalMs = row.totalMs;
            out.perMsg = row.perMsg;
            if (out.isEsp) {
                auto it = espMeta.find(out.module);
                if (it != espMeta.end()) {
                    out.author = it->second.author.empty() ? PlaceholderText() : it->second.author;
                    out.version = FormatVersion(it->second.version);
                } else {
                    out.author = PlaceholderText();
                    out.version = PlaceholderText();
                }
            } else {
                out.author = LookupDllAuthor(out.module);
                out.version = LookupDllVersion(out.module);
            }
            rows.push_back(std::move(out));
        }

        return rows;
    }

    std::vector<std::size_t> BuildExportMessageIndices(const std::vector<std::string_view>& messageNames) {
        std::vector<std::size_t> indices;
        indices.reserve(messageNames.size());
        std::optional<std::size_t> dataLoadedIdx;

        for (std::size_t i = 0; i < messageNames.size(); ++i) {
            if (messageNames[i].find("Game") != std::string_view::npos) continue;
            if (messageNames[i] == "DataLoaded") {
                dataLoadedIdx = i;
                continue;
            }
            indices.push_back(i);
        }

        if (dataLoadedIdx) {
            indices.insert(indices.begin(), *dataLoadedIdx);
        }

        return indices;
    }

    ExportTotals BuildExportTotals(const std::vector<ExportRow>& rows, const std::vector<std::size_t>& msgIndices) {
        ExportTotals totals;
        totals.rowCount = rows.size();

        for (const auto& row : rows) {
            if (row.isEsp) totals.espExtra += row.totalMs;
            for (const auto i : msgIndices) {
                double value = row.perMsg[i];
                if (row.isEsp || value < 1.0) value = 0.0;
                totals.colTotals[i] += value;
            }
        }

        for (const double value : totals.colTotals) totals.grandTotal += value;
        totals.grandTotal += totals.espExtra;
        return totals;
    }

    bool WriteCsv(const std::filesystem::path& path, const SummaryMetrics& summary, const SystemInfo& systemInfo,
                  const std::vector<std::string_view>& messageNames, const std::vector<ExportRow>& rows) {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return false;
        const auto msgIndices = BuildExportMessageIndices(messageNames);
        const auto totals = BuildExportTotals(rows, msgIndices);

        const std::vector<std::pair<std::string, std::string>> summaryRows = {
            {"skse_init_time_heuristic_s", FormatSeconds(summary.skseInitMs)},
            {"total_dll_time_s", FormatSeconds(summary.totalDllMs)},
            {"total_esp_time_s", FormatSeconds(summary.totalEspMs)},
            {"total_time_s", FormatSeconds(summary.totalAllMs)},
            {"dll_count", std::to_string(summary.dllCount)},
            {"esp_count", std::to_string(summary.espCount)},
        };

        const std::vector<std::pair<std::string, std::string>> systemRows = {
            {"skyrim_runtime_variant", EscapeCsv(systemInfo.runtimeVariant)},
            {"skyrim_runtime_version", EscapeCsv(systemInfo.runtimeVersion)},
            {"os_name_version", EscapeCsv(systemInfo.osNameVersion)},
            {"cpu_vendor", EscapeCsv(systemInfo.cpuVendor)},
            {"cpu_model", EscapeCsv(systemInfo.cpuModel)},
            {"gpu_list", EscapeCsv(systemInfo.gpuList)},
        };

        out << "summary_key,summary_s,system_key,system_value\n";
        const auto rowCount = std::max(summaryRows.size(), systemRows.size());
        for (std::size_t i = 0; i < rowCount; ++i) {
            if (i < summaryRows.size()) {
                out << summaryRows[i].first << ',' << summaryRows[i].second;
            } else {
                out << ',';
            }

            out << ',';

            if (i < systemRows.size()) {
                out << systemRows[i].first << ',' << systemRows[i].second;
            } else {
                out << ',';
            }
            out << "\n";
        }
        out << "\n";

        out << "module,author,version,type,total_s";
        for (const auto idx : msgIndices) out << ',' << EscapeCsv(std::string(messageNames[idx]) + "_s");
        out << "\n";

        out << EscapeCsv(fmt::format("{} ({})", Localization::TotalsRowLabel, totals.rowCount)) << ',';
        out << EscapeCsv(PlaceholderText()) << ',';
        out << EscapeCsv(PlaceholderText()) << ',';
        out << EscapeCsv(PlaceholderText()) << ',';
        out << FormatSeconds(totals.grandTotal);
        for (const auto idx : msgIndices) {
            out << ',' << FormatSeconds(totals.colTotals[idx]);
        }
        out << "\n";

        for (const auto& row : rows) {
            out << EscapeCsv(row.module) << ',';
            out << EscapeCsv(row.author) << ',';
            out << EscapeCsv(row.version) << ',';
            out << (row.isEsp ? Localization::TypeEsp : Localization::TypeDll) << ',';
            out << FormatSeconds(row.totalMs);
            for (const auto idx : msgIndices) {
                double value = row.perMsg[idx];
                if (row.isEsp) value = 0.0;
                out << ',' << FormatSeconds(value);
            }
            out << "\n";
        }

        return true;
    }

    bool WriteTxt(const std::filesystem::path& path, const SummaryMetrics& summary, const SystemInfo& systemInfo,
                  const std::vector<std::string_view>& messageNames, const std::vector<ExportRow>& rows) {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return false;
        const auto msgIndices = BuildExportMessageIndices(messageNames);
        const auto totals = BuildExportTotals(rows, msgIndices);

        std::vector<const ExportRow*> sortedRows;
        sortedRows.reserve(rows.size());
        for (const auto& row : rows) sortedRows.push_back(&row);
        std::ranges::sort(sortedRows, [](const ExportRow* a, const ExportRow* b) {
            return a->totalMs > b->totalMs;
        });

        out << Localization::Summary << "\n";
        out << "skse_init_time_heuristic_s: " << FormatSeconds(summary.skseInitMs) << "\n";
        out << "total_dll_time_s: " << FormatSeconds(summary.totalDllMs) << "\n";
        out << "total_esp_time_s: " << FormatSeconds(summary.totalEspMs) << "\n";
        out << "total_time_s: " << FormatSeconds(summary.totalAllMs) << "\n";
        out << "dll_count: " << summary.dllCount << "\n";
        out << "esp_count: " << summary.espCount << "\n\n";

        out << "System\n";
        out << "skyrim_runtime_variant: " << SanitizeText(systemInfo.runtimeVariant) << "\n";
        out << "skyrim_runtime_version: " << SanitizeText(systemInfo.runtimeVersion) << "\n";
        out << "os_name_version: " << SanitizeText(systemInfo.osNameVersion) << "\n";
        out << "cpu_vendor: " << SanitizeText(systemInfo.cpuVendor) << "\n";
        out << "cpu_model: " << SanitizeText(systemInfo.cpuModel) << "\n";
        out << "gpu_list: " << SanitizeText(systemInfo.gpuList) << "\n\n";

        constexpr std::size_t kMaxModuleWidth = 48;
        constexpr std::size_t kMaxAuthorWidth = 28;
        constexpr std::size_t kMaxVersionWidth = 16;

        std::vector<std::string> msgHeaders;
        msgHeaders.reserve(msgIndices.size());
        for (const auto idx : msgIndices) msgHeaders.push_back(std::string(messageNames[idx]) + "_s");

        std::size_t moduleWidth = std::string_view("module").size();
        std::size_t authorWidth = std::string_view("author").size();
        std::size_t versionWidth = std::string_view("version").size();
        std::size_t typeWidth = std::string_view("type").size();
        std::size_t totalWidth = std::string_view("total_s").size();
        std::vector<std::size_t> msgWidths(msgHeaders.size(), 0);
        for (std::size_t i = 0; i < msgHeaders.size(); ++i) msgWidths[i] = msgHeaders[i].size();

        struct RenderedRow {
            std::string module;
            std::string author;
            std::string version;
            std::string type;
            std::string total;
            std::vector<std::string> perMsg;
        };

        std::vector<RenderedRow> rendered;
        rendered.reserve(sortedRows.size());

        {
            RenderedRow rr;
            rr.module = Ellipsize(fmt::format("{} ({})", Localization::TotalsRowLabel, totals.rowCount),
                                  kMaxModuleWidth);
            rr.author = PlaceholderText();
            rr.version = PlaceholderText();
            rr.type = PlaceholderText();
            rr.total = FormatSeconds(totals.grandTotal);
            rr.perMsg.reserve(msgIndices.size());

            moduleWidth = std::min(kMaxModuleWidth, std::max(moduleWidth, rr.module.size()));
            authorWidth = std::min(kMaxAuthorWidth, std::max(authorWidth, rr.author.size()));
            versionWidth = std::min(kMaxVersionWidth, std::max(versionWidth, rr.version.size()));
            typeWidth = std::max(typeWidth, rr.type.size());
            totalWidth = std::max(totalWidth, rr.total.size());

            for (std::size_t c = 0; c < msgIndices.size(); ++c) {
                const auto idx = msgIndices[c];
                const auto cell = FormatSeconds(totals.colTotals[idx]);
                rr.perMsg.push_back(cell);
                if (c < msgWidths.size()) msgWidths[c] = std::max(msgWidths[c], cell.size());
            }

            rendered.push_back(std::move(rr));
        }

        for (const auto* row : sortedRows) {
            RenderedRow rr;
            rr.module = Ellipsize(row->module, kMaxModuleWidth);
            rr.author = Ellipsize(row->author, kMaxAuthorWidth);
            rr.version = Ellipsize(row->version, kMaxVersionWidth);
            rr.type = row->isEsp ? Localization::TypeEsp : Localization::TypeDll;
            rr.total = FormatSeconds(row->totalMs);
            rr.perMsg.reserve(msgIndices.size());

            moduleWidth = std::min(kMaxModuleWidth, std::max(moduleWidth, rr.module.size()));
            authorWidth = std::min(kMaxAuthorWidth, std::max(authorWidth, rr.author.size()));
            versionWidth = std::min(kMaxVersionWidth, std::max(versionWidth, rr.version.size()));
            typeWidth = std::max(typeWidth, rr.type.size());
            totalWidth = std::max(totalWidth, rr.total.size());

            for (std::size_t c = 0; c < msgIndices.size(); ++c) {
                const auto idx = msgIndices[c];
                double value = row->perMsg[idx];
                if (row->isEsp) value = 0.0;
                const auto cell = FormatSeconds(value);
                rr.perMsg.push_back(cell);
                if (c < msgWidths.size()) msgWidths[c] = std::max(msgWidths[c], cell.size());
            }

            rendered.push_back(std::move(rr));
        }

        out << std::left
            << std::setw(static_cast<int>(moduleWidth)) << "module" << "  "
            << std::setw(static_cast<int>(authorWidth)) << "author" << "  "
            << std::setw(static_cast<int>(versionWidth)) << "version" << "  "
            << std::setw(static_cast<int>(typeWidth)) << "type" << "  "
            << std::right << std::setw(static_cast<int>(totalWidth)) << "total_s";
        for (std::size_t i = 0; i < msgHeaders.size(); ++i) {
            out << "  " << std::setw(static_cast<int>(msgWidths[i])) << msgHeaders[i];
        }
        out << "\n";

        out << std::string(moduleWidth, '-') << "  "
            << std::string(authorWidth, '-') << "  "
            << std::string(versionWidth, '-') << "  "
            << std::string(typeWidth, '-') << "  "
            << std::string(totalWidth, '-');
        for (const auto w : msgWidths) out << "  " << std::string(w, '-');
        out << "\n";

        for (const auto& rr : rendered) {
            out << std::left
                << std::setw(static_cast<int>(moduleWidth)) << rr.module << "  "
                << std::setw(static_cast<int>(authorWidth)) << rr.author << "  "
                << std::setw(static_cast<int>(versionWidth)) << rr.version << "  "
                << std::setw(static_cast<int>(typeWidth)) << rr.type << "  "
                << std::right << std::setw(static_cast<int>(totalWidth)) << rr.total;
            for (std::size_t i = 0; i < rr.perMsg.size() && i < msgWidths.size(); ++i) {
                out << "  " << std::setw(static_cast<int>(msgWidths[i])) << rr.perMsg[i];
            }
            out << "\n";
        }

        return true;
    }

    std::string BuildSuccessStatus(const std::filesystem::path& path) {
        if (Localization::ExportStatusSuccess.empty()) return path.string();

        try {
            return fmt::sprintf(Localization::ExportStatusSuccess, path.string().c_str());
        } catch (const fmt::format_error& e) {
            logger::warn("[Profiler] Invalid export success format '{}': {}", Localization::ExportStatusSuccess,
                         e.what());
            return path.string();
        }
    }
}

bool Export::WriteSnapshot(const Format format, std::string& statusMessage) {
    const auto taggedRows = MessagingProfiler::GetTaggedRows();
    const auto summary = BuildSummaryMetrics(taggedRows);
    const auto systemInfo = BuildSystemInfo();
    const auto exportRows = BuildExportRows(taggedRows);
    const auto messageNames = MessagingProfiler::GetMessageTypeNames();
    const auto path = BuildExportPath(format);

    const bool ok = format == Format::Txt
                        ? WriteTxt(path, summary, systemInfo, messageNames, exportRows)
                        : WriteCsv(path, summary, systemInfo, messageNames, exportRows);

    if (ok) {
        statusMessage = BuildSuccessStatus(path);
        return true;
    }

    statusMessage = Localization::ExportStatusFailed;
    logger::error("[Profiler] Failed to export profiling snapshot to '{}'", path.string());
    return false;
}