#include "GameIntegration.h"
#include "PoseMath.h"
#include "TurnController.h"

namespace
{
    using ViGEmError = std::uint32_t;

    constexpr ViGEmError kViGEmErrorNone = 0x20000000;

    struct ViGEmXusbReport
    {
        std::uint16_t buttons{};
        std::uint8_t leftTrigger{};
        std::uint8_t rightTrigger{};
        std::int16_t thumbLX{};
        std::int16_t thumbLY{};
        std::int16_t thumbRX{};
        std::int16_t thumbRY{};
    };
    static_assert(sizeof(ViGEmXusbReport) == 12);

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
        using TargetUpdate =
            ViGEmError (*)(void*, void*, ViGEmXusbReport);

        HMODULE module{ nullptr };
        Alloc alloc{ nullptr };
        Free free{ nullptr };
        Connect connect{ nullptr };
        Disconnect disconnect{ nullptr };
        TargetAlloc targetX360Alloc{ nullptr };
        TargetFree targetFree{ nullptr };
        TargetAdd targetAdd{ nullptr };
        TargetRemove targetRemove{ nullptr };
        TargetUpdate targetX360Update{ nullptr };

        [[nodiscard]] bool Complete() const
        {
            return module &&
                alloc &&
                free &&
                connect &&
                disconnect &&
                targetX360Alloc &&
                targetFree &&
                targetAdd &&
                targetRemove &&
                targetX360Update;
        }
    };

    ViGEmApi g_vigem;

    [[nodiscard]] bool ViGEmSucceeded(ViGEmError error)
    {
        return error == kViGEmErrorNone;
    }

    [[nodiscard]] std::filesystem::path ViGEmClientPath()
    {
        HMODULE pluginModule = nullptr;
        const auto address = reinterpret_cast<LPCWSTR>(
            reinterpret_cast<std::uintptr_t>(&ViGEmClientPath));
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address,
                &pluginModule)) {
            return {};
        }

        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(
            pluginModule,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) {
            return {};
        }
        path.resize(length);
        return std::filesystem::path(path).parent_path() / L"ViGEmClient.dll";
    }

    [[nodiscard]] bool LoadViGEmApi()
    {
        if (g_vigem.Complete()) {
            return true;
        }

        const auto path = ViGEmClientPath();
        if (path.empty()) {
            logger::warn("unable to resolve the SKSE plugin directory");
            return false;
        }

        g_vigem.module = LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!g_vigem.module) {
            logger::warn(
                "unable to load {} (Win32 error {})",
                path.string(),
                GetLastError());
            return false;
        }

        g_vigem.alloc = reinterpret_cast<ViGEmApi::Alloc>(
            GetProcAddress(g_vigem.module, "vigem_alloc"));
        g_vigem.free = reinterpret_cast<ViGEmApi::Free>(
            GetProcAddress(g_vigem.module, "vigem_free"));
        g_vigem.connect = reinterpret_cast<ViGEmApi::Connect>(
            GetProcAddress(g_vigem.module, "vigem_connect"));
        g_vigem.disconnect = reinterpret_cast<ViGEmApi::Disconnect>(
            GetProcAddress(g_vigem.module, "vigem_disconnect"));
        g_vigem.targetX360Alloc = reinterpret_cast<ViGEmApi::TargetAlloc>(
            GetProcAddress(g_vigem.module, "vigem_target_x360_alloc"));
        g_vigem.targetFree = reinterpret_cast<ViGEmApi::TargetFree>(
            GetProcAddress(g_vigem.module, "vigem_target_free"));
        g_vigem.targetAdd = reinterpret_cast<ViGEmApi::TargetAdd>(
            GetProcAddress(g_vigem.module, "vigem_target_add"));
        g_vigem.targetRemove = reinterpret_cast<ViGEmApi::TargetRemove>(
            GetProcAddress(g_vigem.module, "vigem_target_remove"));
        g_vigem.targetX360Update = reinterpret_cast<ViGEmApi::TargetUpdate>(
            GetProcAddress(g_vigem.module, "vigem_target_x360_update"));

        if (!g_vigem.Complete()) {
            logger::warn("ViGEmClient.dll is missing one or more required exports");
            FreeLibrary(g_vigem.module);
            g_vigem = {};
            return false;
        }

        logger::info("ViGEmClient runtime loaded from {}", path.string());
        return true;
    }
}

namespace HDT
{
    GameIntegration& GameIntegration::GetSingleton()
    {
        static GameIntegration singleton;
        return singleton;
    }

    GameIntegration::~GameIntegration()
    {
        TeardownViGEmTarget();
    }

    bool GameIntegration::InitializeOutput()
    {
        if (outputInitialized_) {
            return outputReady_;
        }
        outputInitialized_ = true;
        outputReady_ = InstallViGEmTarget();
        if (!outputReady_) {
            logger::warn(
                "ViGEm output unavailable; diagnostic mode remains usable");
        }
        return outputReady_;
    }

    bool GameIntegration::Initialize()
    {
        if (initialized_) {
            return ready_;
        }
        initialized_ = true;

        if (!REL::Module::IsVR()) {
            failureReason_ = "unsupported non-VR runtime";
            logger::critical("Game integration unavailable: {}", failureReason_);
            return false;
        }

        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetVRNodeData()) {
            failureReason_ = "Skyrim VR player data is unavailable";
            logger::critical("Game integration unavailable: {}", failureReason_);
            return false;
        }

        const auto inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            failureReason_ = "Skyrim input manager is unavailable";
            logger::critical("Game integration unavailable: {}", failureReason_);
            return false;
        }
        inputManager->AddEventSink(this);

        (void)InitializeOutput();

        // Actor::Update is slot 0xAF in Skyrim VR. The relocation resolves the
        // PlayerCharacter vtable through the VR Address Library, not a raw address.
        constexpr std::size_t playerUpdateIndex = 0xAF;
        REL::Relocation<std::uintptr_t> playerVtable{ RE::PlayerCharacter::VTABLE[0] };
        originalPlayerUpdate_ = playerVtable.write_vfunc(
            playerUpdateIndex,
            PlayerUpdateHook);

        if (originalPlayerUpdate_.address() == 0) {
            failureReason_ = "unable to install the player update hook";
            logger::critical("Game integration unavailable: {}", failureReason_);
            return false;
        }

        ready_ = true;
        failureReason_.clear();
        logger::info("VR pose diagnostics initialized; player update hook installed");
        return true;
    }

    bool GameIntegration::IsReady() const
    {
        return ready_;
    }

    const std::string& GameIntegration::FailureReason() const
    {
        return failureReason_;
    }

    std::optional<PoseSample> GameIntegration::ReadPose() const
    {
        if (!ready_ || !centerOffsetDegrees_) {
            return std::nullopt;
        }

        auto sample = ReadRawPose();
        if (!sample) {
            return std::nullopt;
        }

        sample->relativeYawDegrees =
            NormalizeDegrees(sample->relativeYawDegrees - *centerOffsetDegrees_);
        return sample;
    }

    std::optional<PoseSample> GameIntegration::ReadRawPose() const
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return std::nullopt;
        }

        const auto vrNodes = player->GetVRNodeData();
        if (!vrNodes || !vrNodes->UprightHmdNode || !vrNodes->RoomNode) {
            return std::nullopt;
        }

        RE::NiPoint3 hmdEuler{};
        RE::NiPoint3 roomEuler{};
        if (!vrNodes->UprightHmdNode->world.rotate.ToEulerAnglesXYZ(hmdEuler) ||
            !vrNodes->RoomNode->world.rotate.ToEulerAnglesXYZ(roomEuler)) {
            return std::nullopt;
        }

        const auto hmdYaw = NormalizeDegrees(RadiansToDegrees(hmdEuler.z));
        const auto bodyYaw = NormalizeDegrees(RadiansToDegrees(roomEuler.z));
        return PoseSample{
            hmdYaw,
            bodyYaw,
            NormalizeDegrees(hmdYaw - bodyYaw)
        };
    }

    bool GameIntegration::IsGameFocused() const
    {
        // Neither Win32 foreground state nor Main::gameActive reflects HMD
        // focus reliably under OpenComposite. Menu and lifecycle guards remain.
        return true;
    }

    bool GameIntegration::ApplyTurnInput(float normalizedInput)
    {
        normalizedInput = std::clamp(normalizedInput, -1.0F, 1.0F);

        if (!outputReady_ || !vigemClient_ || !vigemTarget_) {
            return normalizedInput == 0.0F;
        }

        ViGEmXusbReport report{};
        const auto scaled = std::lround(normalizedInput * 32767.0F);
        report.thumbRX = static_cast<std::int16_t>(
            std::clamp<long>(scaled, -32768L, 32767L));

        const auto err = g_vigem.targetX360Update(
            vigemClient_,
            vigemTarget_,
            report);
        if (!ViGEmSucceeded(err)) {
            if (!vigemFailureLogged_.exchange(true, std::memory_order_relaxed)) {
                logger::error(
                    "ViGEm update failed (0x{:08x}); turning output disabled",
                    static_cast<std::uint32_t>(err));
            }
            return false;
        }

        if (std::abs(normalizedInput) >= 0.001F) {
            constexpr std::uint32_t maximumInjectionLines = 120;
            const auto line =
                injectionTraceLines_.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;
            if (line <= maximumInjectionLines) {
                logger::debug(
                    "vigem inject line={} requested={:.3f} sThumbRX={}",
                    line,
                    normalizedInput,
                    static_cast<int>(report.thumbRX));
            }
        }
        return true;
    }

    bool GameIntegration::InstallViGEmTarget()
    {
        // Allocate client and connect to the ViGEmBus kernel driver. Failure
        // here almost always means the driver is not installed.
        if (!LoadViGEmApi()) {
            return false;
        }

        auto client = g_vigem.alloc();
        if (!client) {
            logger::warn("ViGEm client allocation failed");
            return false;
        }
        auto err = g_vigem.connect(client);
        if (!ViGEmSucceeded(err)) {
            logger::warn(
                "ViGEmBus connection failed (0x{:08x}); install the ViGEmBus "
                "driver from https://github.com/nefarius/ViGEmBus/releases",
                static_cast<std::uint32_t>(err));
            g_vigem.free(client);
            return false;
        }

        auto target = g_vigem.targetX360Alloc();
        if (!target) {
            logger::warn("ViGEm target_x360 allocation failed");
            g_vigem.disconnect(client);
            g_vigem.free(client);
            return false;
        }
        err = g_vigem.targetAdd(client, target);
        if (!ViGEmSucceeded(err)) {
            logger::warn(
                "ViGEm target_add failed (0x{:08x})",
                static_cast<std::uint32_t>(err));
            g_vigem.targetFree(target);
            g_vigem.disconnect(client);
            g_vigem.free(client);
            return false;
        }

        vigemClient_ = client;
        vigemTarget_ = target;
        logger::info(
            "ViGEm virtual Xbox 360 pad plugged in (turn -> right stick)");
        return true;
    }

    void GameIntegration::TeardownViGEmTarget()
    {
        if (vigemClient_ && vigemTarget_) {
            (void)g_vigem.targetRemove(vigemClient_, vigemTarget_);
        }
        if (vigemTarget_) {
            g_vigem.targetFree(vigemTarget_);
            vigemTarget_ = nullptr;
        }
        if (vigemClient_) {
            g_vigem.disconnect(vigemClient_);
            g_vigem.free(vigemClient_);
            vigemClient_ = nullptr;
        }
        if (g_vigem.module) {
            FreeLibrary(g_vigem.module);
            g_vigem = {};
        }
    }

    RE::BSEventNotifyControl GameIntegration::ProcessEvent(
        RE::InputEvent* const* events,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!events || !*events || tracedThumbstickEvents_ >= 200) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto event = *events; event; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kThumbstick) {
                continue;
            }

            const auto thumbstick = static_cast<RE::ThumbstickEvent*>(event);
            if (std::abs(thumbstick->xValue) < 0.01F &&
                std::abs(thumbstick->yValue) < 0.01F) {
                continue;
            }

            logger::debug(
                "{} thumbstick device={} id={} event='{}' x={:.3f} y={:.3f}",
                "physical",
                static_cast<std::int32_t>(event->GetDevice()),
                thumbstick->GetIDCode(),
                thumbstick->userEvent.c_str(),
                thumbstick->xValue,
                thumbstick->yValue);
            ++tracedThumbstickEvents_;
            if (tracedThumbstickEvents_ >= 200) {
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void GameIntegration::PlayerUpdateHook(RE::Actor* actor, float deltaSeconds)
    {
        auto& integration = GetSingleton();
        integration.UpdateAutomaticCenter(deltaSeconds);
        TurnController::GetSingleton().OnFrame(deltaSeconds);
        integration.originalPlayerUpdate_(actor, deltaSeconds);

        integration.hookLogAccumulator_ += deltaSeconds;
        if (integration.hookLogAccumulator_ >= 1.0F) {
            logger::debug(
                "player update hook active; poseAvailable={} focused={}",
                integration.ReadPose().has_value(),
                integration.IsGameFocused());
            integration.hookLogAccumulator_ = 0.0F;
        }
    }

    void GameIntegration::UpdateAutomaticCenter(float deltaSeconds)
    {
        constexpr auto calibrationDuration = 2.0F;
        constexpr auto maximumSafeFrameTime = 0.1F;

        if (centerOffsetDegrees_ ||
            deltaSeconds <= 0.0F ||
            deltaSeconds > maximumSafeFrameTime) {
            return;
        }

        const auto sample = ReadRawPose();
        if (!sample) {
            return;
        }

        const auto radians =
            sample->relativeYawDegrees * (std::numbers::pi_v<float> / 180.0F);
        calibrationSinSum_ += std::sin(radians);
        calibrationCosSum_ += std::cos(radians);
        ++calibrationSamples_;
        calibrationElapsed_ += deltaSeconds;

        if (calibrationElapsed_ >= calibrationDuration && calibrationSamples_ > 0) {
            centerOffsetDegrees_ = NormalizeDegrees(RadiansToDegrees(
                std::atan2(calibrationSinSum_, calibrationCosSum_)));
            logger::info(
                "Automatic center calibrated at {:.2f} degrees from {} samples",
                *centerOffsetDegrees_,
                calibrationSamples_);
        }
    }
}
