#pragma once

namespace Utilities {
    std::optional<std::uint32_t> hex_to_u32(std::string_view s);

    std::string WideToUtf8(const std::wstring& ws);

    // Utility, kept from prev
    #ifndef NDEBUG
    size_t GetModuleSize(HMODULE module);
    bool IsAddressInSkyrimExe(uintptr_t addr);
    void PrintStackTrace();
    #endif
};