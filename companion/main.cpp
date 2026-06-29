#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include "CompanionProtocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
#include <iomanip>

namespace
{
    using ViGEmError = std::uint32_t;
    constexpr ViGEmError kViGEmErrorNone = 0x20000000;

    struct XusbReport
    {
        std::uint16_t buttons{};
        std::uint8_t leftTrigger{};
        std::uint8_t rightTrigger{};
        std::int16_t thumbLX{};
        std::int16_t thumbLY{};
        std::int16_t thumbRX{};
        std::int16_t thumbRY{};
    };
    static_assert(sizeof(XusbReport) == 12);

    struct ViGEmApi
    {
        using Alloc = void* (*)();
        using Free = void (*)(void*);
        using Connect = ViGEmError (*)(void*);
        using Disconnect = void (*)(void*);
        using TargetAlloc = void* (*)();
        using TargetFree = void (*)(void*);
        using TargetAdd = ViGEmError (*)(void*, void*);
        using TargetRemove = ViGEmError (*)(void*, void*);
        using TargetUpdate = ViGEmError (*)(void*, void*, XusbReport);
        using TargetGetUserIndex = ViGEmError (*)(void*, void*, unsigned long*);

        HMODULE module{};
        Alloc alloc{};
        Free free{};
        Connect connect{};
        Disconnect disconnect{};
        TargetAlloc targetX360Alloc{};
        TargetFree targetFree{};
        TargetAdd targetAdd{};
        TargetRemove targetRemove{};
        TargetUpdate targetX360Update{};
        TargetGetUserIndex targetX360GetUserIndex{};

        [[nodiscard]] bool Complete() const
        {
            return module && alloc && free && connect && disconnect &&
                targetX360Alloc && targetFree && targetAdd && targetRemove &&
                targetX360Update && targetX360GetUserIndex;
        }
    };

    std::atomic<bool> g_running{ true };

    BOOL WINAPI ConsoleHandler(DWORD signal)
    {
        if (signal == CTRL_C_EVENT ||
            signal == CTRL_CLOSE_EVENT ||
            signal == CTRL_BREAK_EVENT ||
            signal == CTRL_SHUTDOWN_EVENT) {
            g_running.store(false);
            return TRUE;
        }
        return FALSE;
    }

    [[nodiscard]] std::filesystem::path ExecutableDirectory()
    {
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) {
            return {};
        }
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }

    [[nodiscard]] ViGEmApi LoadViGEm()
    {
        ViGEmApi api;
        const auto path = ExecutableDirectory() / L"ViGEmClient.dll";
        api.module = LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!api.module) {
            std::wcerr << L"Unable to load " << path
                       << L" (Win32 error " << GetLastError() << L")\n";
            return {};
        }

        api.alloc = reinterpret_cast<ViGEmApi::Alloc>(
            GetProcAddress(api.module, "vigem_alloc"));
        api.free = reinterpret_cast<ViGEmApi::Free>(
            GetProcAddress(api.module, "vigem_free"));
        api.connect = reinterpret_cast<ViGEmApi::Connect>(
            GetProcAddress(api.module, "vigem_connect"));
        api.disconnect = reinterpret_cast<ViGEmApi::Disconnect>(
            GetProcAddress(api.module, "vigem_disconnect"));
        api.targetX360Alloc = reinterpret_cast<ViGEmApi::TargetAlloc>(
            GetProcAddress(api.module, "vigem_target_x360_alloc"));
        api.targetFree = reinterpret_cast<ViGEmApi::TargetFree>(
            GetProcAddress(api.module, "vigem_target_free"));
        api.targetAdd = reinterpret_cast<ViGEmApi::TargetAdd>(
            GetProcAddress(api.module, "vigem_target_add"));
        api.targetRemove = reinterpret_cast<ViGEmApi::TargetRemove>(
            GetProcAddress(api.module, "vigem_target_remove"));
        api.targetX360Update = reinterpret_cast<ViGEmApi::TargetUpdate>(
            GetProcAddress(api.module, "vigem_target_x360_update"));
        api.targetX360GetUserIndex =
            reinterpret_cast<ViGEmApi::TargetGetUserIndex>(
                GetProcAddress(
                    api.module,
                    "vigem_target_x360_get_user_index"));

        if (!api.Complete()) {
            std::cerr << "ViGEmClient.dll is missing required exports.\n";
            FreeLibrary(api.module);
            return {};
        }
        return api;
    }

    struct XInputGamepad
    {
        std::uint16_t buttons{};
        std::uint8_t leftTrigger{};
        std::uint8_t rightTrigger{};
        std::int16_t thumbLX{};
        std::int16_t thumbLY{};
        std::int16_t thumbRX{};
        std::int16_t thumbRY{};
    };
    struct XInputState
    {
        std::uint32_t packetNumber{};
        XInputGamepad gamepad{};
    };
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputState*);

    struct XInputApi
    {
        HMODULE module{};
        XInputGetStateFn getState{};
        std::wstring moduleName{};
    };

    [[nodiscard]] XInputApi LoadXInput()
    {
        for (const auto* name :
             { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" }) {
            const auto module = LoadLibraryExW(
                name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) {
                continue;
            }
            const auto getState = reinterpret_cast<XInputGetStateFn>(
                GetProcAddress(module, "XInputGetState"));
            if (getState) {
                return { module, getState, name };
            }
            FreeLibrary(module);
        }
        return {};
    }

    [[nodiscard]] std::uint32_t ReadXInputSlots(
        const XInputApi& xinput,
        XInputState (&states)[4])
    {
        std::uint32_t mask = 0;
        if (!xinput.getState) {
            return mask;
        }
        for (DWORD index = 0; index < 4; ++index) {
            states[index] = {};
            if (xinput.getState(index, &states[index]) == ERROR_SUCCESS) {
                mask |= 1U << index;
            }
        }
        return mask;
    }

    [[nodiscard]] bool IsSkyrimRunning()
    {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"SkyrimVR.exe") == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    [[nodiscard]] std::int32_t AtomicRead32(
        const std::int32_t& value)
    {
        return InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(
                const_cast<std::int32_t*>(&value)),
            0,
            0);
    }

    [[nodiscard]] std::uint32_t AtomicReadUnsigned32(
        const std::uint32_t& value)
    {
        return static_cast<std::uint32_t>(InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(
                const_cast<std::uint32_t*>(&value)),
            0,
            0));
    }

    [[nodiscard]] std::uint64_t AtomicRead64(
        const std::uint64_t& value)
    {
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(
                const_cast<std::uint64_t*>(&value)),
            0,
            0));
    }
}

int main()
{
    SetConsoleTitleW(L"Head Directed Turning Companion");
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    std::cout << "Head Directed Turning Companion\n";
    std::cout << "Start this program before launching Skyrim VR.\n\n";

    auto vigem = LoadViGEm();
    if (!vigem.Complete()) {
        std::cerr << "Companion startup failed.\n";
        return 1;
    }

    auto client = vigem.alloc();
    if (!client) {
        std::cerr << "ViGEm client allocation failed.\n";
        FreeLibrary(vigem.module);
        return 1;
    }

    auto error = vigem.connect(client);
    if (error != kViGEmErrorNone) {
        std::cerr << "ViGEmBus connection failed (0x" << std::hex
                  << error << std::dec << "). Is ViGEmBus installed?\n";
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    auto target = vigem.targetX360Alloc();
    if (!target) {
        std::cerr << "Xbox 360 target allocation failed.\n";
        vigem.disconnect(client);
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    error = vigem.targetAdd(client, target);
    if (error != kViGEmErrorNone) {
        std::cerr << "Virtual controller creation failed (0x" << std::hex
                  << error << std::dec << ").\n";
        vigem.targetFree(target);
        vigem.disconnect(client);
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    // Match vgamepad.VX360Gamepad construction exactly: create a persistent
    // zeroed report and submit it once immediately after attaching the target.
    XusbReport report{};
    error = vigem.targetX360Update(client, target, report);
    if (error != kViGEmErrorNone) {
        std::cerr << "Initial centered report failed (0x" << std::hex
                  << error << std::dec << ").\n";
        vigem.targetRemove(client, target);
        vigem.targetFree(target);
        vigem.disconnect(client);
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    const auto mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(HDT::CompanionProtocol::State),
        HDT::CompanionProtocol::mappingName);
    if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "Another Head Directed Turning Companion is running.\n";
        if (mapping) {
            CloseHandle(mapping);
        }
        vigem.targetRemove(client, target);
        vigem.targetFree(target);
        vigem.disconnect(client);
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    auto state = static_cast<HDT::CompanionProtocol::State*>(
        MapViewOfFile(
            mapping,
            FILE_MAP_READ | FILE_MAP_WRITE,
            0,
            0,
            sizeof(HDT::CompanionProtocol::State)));
    if (!state) {
        std::cerr << "Unable to create the plugin communication channel.\n";
        CloseHandle(mapping);
        vigem.targetRemove(client, target);
        vigem.targetFree(target);
        vigem.disconnect(client);
        vigem.free(client);
        FreeLibrary(vigem.module);
        return 1;
    }

    *state = {};
    state->magicValue = HDT::CompanionProtocol::magic;
    state->versionValue = HDT::CompanionProtocol::version;
    state->companionProcessId = GetCurrentProcessId();

    const auto logPath =
        ExecutableDirectory() / L"HeadDirectedTurningCompanion.log";
    std::ofstream log(logPath, std::ios::trunc);
    log << "# HeadDirectedTurning companion diagnostic log\n"
        << "# protocol_version=" << HDT::CompanionProtocol::version
        << " companion_pid=" << GetCurrentProcessId()
        << " executable=" << ExecutableDirectory().string() << '\n'
        << "# vigem_dll="
        << (ExecutableDirectory() / L"ViGEmClient.dll").string() << '\n';

    auto xinput = LoadXInput();
    log << "# xinput_module="
        << (xinput.module ? std::filesystem::path(xinput.moduleName).string()
                          : "UNAVAILABLE")
        << '\n';

    unsigned long userIndex = 0;
    auto userIndexError = vigem.targetX360GetUserIndex(
        client, target, &userIndex);
    // Device arrival can lag target creation briefly.
    for (int attempt = 0;
         userIndexError != kViGEmErrorNone && attempt < 20;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        userIndexError = vigem.targetX360GetUserIndex(
            client, target, &userIndex);
    }
    state->vigemUserIndex =
        userIndexError == kViGEmErrorNone ?
            static_cast<std::int32_t>(userIndex) :
            -1;
    log << "# vigem_user_index=" << state->vigemUserIndex
        << " get_user_index_error=0x" << std::hex << std::setw(8)
        << std::setfill('0') << userIndexError << std::dec << '\n';

    XInputState xinputStates[4]{};
    auto xinputMask = ReadXInputSlots(xinput, xinputStates);
    state->connectedXInputSlots = xinputMask;
    log << "# initial_xinput_slot_mask=0x" << std::hex << xinputMask
        << std::dec << '\n';
    for (std::uint32_t index = 0; index < 4; ++index) {
        if ((xinputMask & (1U << index)) == 0) {
            log << "# xinput_slot_" << index << "=disconnected\n";
            continue;
        }
        const auto& gamepad = xinputStates[index].gamepad;
        log << "# xinput_slot_" << index << "=connected packet="
            << xinputStates[index].packetNumber << " rx="
            << gamepad.thumbRX << " ry=" << gamepad.thumbRY
            << " lx=" << gamepad.thumbLX << " ly=" << gamepad.thumbLY
            << " buttons=0x" << std::hex << gamepad.buttons << std::dec
            << '\n';
    }
    log << "time_ms,event,sequence,requested_rx,applied_rx,vigem_error,"
           "plugin_heartbeat_age_ms,companion_loop_delay_ms,skyrim_running,"
           "watchdog_active,xinput_slot_mask,vigem_user_index\n";
    log.flush();

    std::cout << "Virtual Xbox 360 controller is ready.\n";
    std::cout << "ViGEm XInput user index: "
              << state->vigemUserIndex << "\n";
    std::cout << "Waiting for SkyrimVR.exe... Press Ctrl+C to stop.\n";
    std::cout << "Applied reports are logged to "
              << logPath.string() << "\n";

    bool sawSkyrim = false;
    bool skyrimRunning = false;
    auto lastProcessCheck = std::uint64_t{ 0 };
    auto lastSequence = std::uint32_t{ 0 };
    auto previousXInputMask = xinputMask;
    auto lastXInputCheck = std::uint64_t{ 0 };
    auto previousLoop = GetTickCount64();
    bool previousWatchdog = true;

    while (g_running.load()) {
        const auto now = GetTickCount64();
        const auto loopDelay = now - previousLoop;
        previousLoop = now;
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(
                &state->companionHeartbeatMilliseconds),
            static_cast<LONG64>(now));
        if (now - lastProcessCheck >= 250) {
            lastProcessCheck = now;
            const auto runningNow = IsSkyrimRunning();
            if (runningNow && !skyrimRunning) {
                std::cout << "Skyrim VR detected; forwarding head turning.\n";
            }
            skyrimRunning = runningNow;
            sawSkyrim = sawSkyrim || runningNow;
            if (sawSkyrim && !runningNow) {
                std::cout << "Skyrim VR exited; shutting down.\n";
                break;
            }
        }
        if (now - lastXInputCheck >= 1000) {
            lastXInputCheck = now;
            xinputMask = ReadXInputSlots(xinput, xinputStates);
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(
                    &state->connectedXInputSlots),
                static_cast<LONG>(xinputMask));
            if (xinputMask != previousXInputMask) {
                log << now << ",xinput_slots_changed,0,0,0,0,0,"
                    << loopDelay << ',' << skyrimRunning << ",0,0x"
                    << std::hex << xinputMask << std::dec << ','
                    << state->vigemUserIndex << '\n';
                log.flush();
                previousXInputMask = xinputMask;
            }
        }

        const auto heartbeat = AtomicRead64(state->heartbeatMilliseconds);
        auto desiredStick = AtomicRead32(state->stickRX);
        const auto sequence = AtomicReadUnsigned32(state->sequence);
        const auto heartbeatAge =
            heartbeat != 0 && now >= heartbeat ? now - heartbeat : 0;
        const auto watchdog =
            heartbeat == 0 ||
            heartbeatAge > HDT::CompanionProtocol::watchdogMilliseconds;
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&state->watchdogActive),
            watchdog ? 1 : 0);
        if (watchdog != previousWatchdog) {
            log << now << ",watchdog_" << (watchdog ? "active" : "clear")
                << ',' << sequence << ',' << desiredStick
                << ",0,0," << heartbeatAge << ',' << loopDelay << ','
                << skyrimRunning << ',' << watchdog << ",0x" << std::hex
                << xinputMask << std::dec << ',' << state->vigemUserIndex
                << '\n';
            log.flush();
            previousWatchdog = watchdog;
        }
        if (watchdog) {
            desiredStick = 0;
        }
        desiredStick = std::clamp(desiredStick, -32768, 32767);

        // vgamepad calls update for every new source sample, even when the
        // numerical axis value did not change. Mirror that behavior exactly.
        if (sequence != lastSequence) {
            report.thumbRX = static_cast<std::int16_t>(desiredStick);
            error = vigem.targetX360Update(client, target, report);
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(&state->lastViGEmError),
                static_cast<LONG>(error));
            if (error != kViGEmErrorNone) {
                std::cerr << "ViGEm update failed (0x" << std::hex
                          << error << std::dec << ").\n";
                break;
            }
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(&state->appliedStickRX),
                desiredStick);
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(&state->appliedSequence),
                static_cast<LONG>(sequence));
            log << now << ",report," << sequence << ',' << desiredStick
                << ',' << desiredStick << ",0x" << std::hex << error
                << std::dec << ',' << heartbeatAge << ',' << loopDelay
                << ',' << skyrimRunning << ',' << watchdog << ",0x"
                << std::hex << xinputMask << std::dec << ','
                << state->vigemUserIndex << '\n';
            log.flush();
            lastSequence = sequence;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    report = {};
    vigem.targetX360Update(client, target, report);
    UnmapViewOfFile(state);
    CloseHandle(mapping);
    vigem.targetRemove(client, target);
    vigem.targetFree(target);
    vigem.disconnect(client);
    vigem.free(client);
    if (xinput.module) {
        FreeLibrary(xinput.module);
    }
    FreeLibrary(vigem.module);
    return 0;
}
