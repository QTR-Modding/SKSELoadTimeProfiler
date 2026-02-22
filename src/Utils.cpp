#include "Utils.h"
#include <psapi.h>

std::filesystem::path Utilities::GetLogPath() {
    const auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    return logFilePath;
}

std::vector<std::string> Utilities::ReadLogFile() { // NOLINT(misc-use-internal-linkage)
    std::vector<std::string> logLines;

    std::ifstream file(GetLogPath().c_str());
    if (!file.is_open()) {
        return logLines;
    }

    std::string line;
    while (std::getline(file, line)) {
        logLines.push_back(line);
    }

    file.close();

    return logLines;
}

std::optional<std::uint32_t> Utilities::hex_to_u32(std::string_view s) {
    auto is_space = [](const unsigned char c) { return std::isspace(c); };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);

    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);

    if (s.empty() || s.size() > 8) return std::nullopt; // >32 bits

    std::uint32_t value{};
    const char* first = s.data();
    const char* last = s.data() + s.size();

    auto [ptr, ec] = std::from_chars(first, last, value, 16);

    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return value;
}

std::string Utilities::WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
    out.pop_back();
    return out;
}

size_t Utilities::GetModuleSize(const HMODULE module) {
    MODULEINFO moduleInfo;
    if (GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo))) {
        return moduleInfo.SizeOfImage;
    }
    return 0;
}

bool Utilities::IsAddressInSkyrimExe(const uintptr_t addr) {
    void* skyrimBase = GetModuleHandle(L"SkyrimSE.exe");
    if (!skyrimBase) return false;
    const size_t skyrimSize = GetModuleSize(static_cast<HMODULE>(skyrimBase));
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(skyrimBase);
    return addr >= baseAddr && addr < (baseAddr + skyrimSize);
}

void Utilities::PrintStackTrace() {
    void* skyrimSEBase = GetModuleHandle(L"SkyrimSE.exe");
    const auto skyrimAddress = reinterpret_cast<uintptr_t>(skyrimSEBase);
    constexpr int maxFrames = 64;
    void* stack[maxFrames];
    const unsigned short frames = CaptureStackBackTrace(0, maxFrames, stack, nullptr);
    for (unsigned short i = 0; i < frames; ++i) {
        const auto currentAddress = reinterpret_cast<uintptr_t>(stack[i]);
        if (IsAddressInSkyrimExe(currentAddress)) {
            logger::critical("{:x}", 0x140000000 + currentAddress - skyrimAddress);
        }
    }
}