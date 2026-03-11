#pragma once

namespace Export {
    
    constexpr std::size_t kMaxExportFiles = 20;

    enum class Format {
        Csv = 0,
        Txt = 1,
        Json = 2,
        kTotal
    };

    bool WriteSnapshot(Format format, std::string& statusMessage);
}