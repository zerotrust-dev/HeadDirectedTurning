#pragma once

#include <cstdint>
#include <string_view>

namespace HDT::Version
{
    inline constexpr std::string_view name = "Head Directed Turning";
    inline constexpr std::string_view semantic = HDT_VERSION;
    inline constexpr std::uint32_t settingsSchema = 1;
    inline constexpr std::uint32_t nativeApi = 1;
}
