#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include "CompanionProtocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

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

        [[nodiscard]] bool Complete() const
        {
            return module && alloc && free && connect && disconnect &&
                targetX360Alloc && targetFree && targetAdd && targetRemove &&
                targetX360Update;
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

        if (!api.Complete()) {
            std::cerr << "ViGEmClient.dll is missing required exports.\n";
            FreeLibrary(api.module);
            return {};
        }
        return api;
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

    std::cout << "Virtual Xbox 360 controller is ready.\n";
    std::cout << "Waiting for SkyrimVR.exe... Press Ctrl+C to stop.\n";

    bool sawSkyrim = false;
    bool skyrimRunning = false;
    auto lastProcessCheck = std::uint64_t{ 0 };
    auto lastReportTime = std::uint64_t{ 0 };
    auto lastStick = std::int32_t{ 0 };

    while (g_running.load()) {
        const auto now = GetTickCount64();
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

        const auto heartbeat = AtomicRead64(state->heartbeatMilliseconds);
        auto desiredStick = AtomicRead32(state->stickRX);
        if (!skyrimRunning ||
            heartbeat == 0 ||
            now - heartbeat >
                HDT::CompanionProtocol::watchdogMilliseconds) {
            desiredStick = 0;
        }
        desiredStick = std::clamp(desiredStick, -32768, 32767);

        if (desiredStick != lastStick || now - lastReportTime >= 100) {
            XusbReport report{};
            report.thumbRX = static_cast<std::int16_t>(desiredStick);
            error = vigem.targetX360Update(client, target, report);
            if (error != kViGEmErrorNone) {
                std::cerr << "ViGEm update failed (0x" << std::hex
                          << error << std::dec << ").\n";
                break;
            }
            lastStick = desiredStick;
            lastReportTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    XusbReport centered{};
    vigem.targetX360Update(client, target, centered);
    UnmapViewOfFile(state);
    CloseHandle(mapping);
    vigem.targetRemove(client, target);
    vigem.targetFree(target);
    vigem.disconnect(client);
    vigem.free(client);
    FreeLibrary(vigem.module);
    return 0;
}
