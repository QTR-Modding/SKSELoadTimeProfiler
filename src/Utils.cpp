#include "Utils.h"

std::filesystem::path Utilities::GetLogPath() {
    const auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    return logFilePath;
}

std::vector<std::string> Utilities::ReadLogFile() {
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
    auto is_space = [](const unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back()))  s.remove_suffix(1);

    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);

    if (s.empty() || s.size() > 8) return std::nullopt; // >32 bits

    std::uint32_t value{};
    const char* first = s.data();
    const char* last  = s.data() + s.size();

    auto [ptr, ec] = std::from_chars(first, last, value, 16);

    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return value;
}