#pragma once

#include <cstdint>

namespace HDT::CompanionProtocol
{
    inline constexpr wchar_t mappingName[] =
        L"Local\\HeadDirectedTurning.Output.v2";
    inline constexpr std::uint32_t magic = 0x32544448;  // "HDT2"
    inline constexpr std::uint32_t version = 2;
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
        std::int32_t appliedStickRX{};
        std::uint32_t appliedSequence{};
        std::uint32_t lastViGEmError{};
        std::uint64_t companionHeartbeatMilliseconds{};
    };

    static_assert(sizeof(State) == 48);
}
