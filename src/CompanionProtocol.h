#pragma once

#include <cstdint>

namespace HDT::CompanionProtocol
{
    inline constexpr wchar_t mappingName[] =
        L"Local\\HeadDirectedTurning.Output.v1";
    inline constexpr std::uint32_t magic = 0x31544448;  // "HDT1"
    inline constexpr std::uint32_t version = 1;
    inline constexpr std::uint64_t watchdogMilliseconds = 250;

    // The companion creates and initializes this mapping before Skyrim starts.
    // The plugin is the single writer for the volatile output fields.
    struct alignas(8) State
    {
        std::uint32_t magicValue{};
        std::uint32_t versionValue{};
        std::int32_t stickRX{};
        std::uint32_t sequence{};
        std::uint64_t heartbeatMilliseconds{};
        std::uint32_t pluginProcessId{};
        std::uint32_t reserved{};
    };

    static_assert(sizeof(State) == 32);
}
