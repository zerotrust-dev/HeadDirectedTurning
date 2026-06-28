#include "GameIntegration.h"
#include "PoseMath.h"
#include "TurnController.h"

namespace HDT
{
    GameIntegration& GameIntegration::GetSingleton()
    {
        static GameIntegration singleton;
        return singleton;
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
        outputReady_ = InstallControllerStateHook();
        if (!outputReady_) {
            logger::warn(
                "OpenVR controller-state output unavailable; diagnostic mode remains usable");
        }

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
        requestedTurnInput_.store(
            std::clamp(normalizedInput, -1.0F, 1.0F),
            std::memory_order_relaxed);
        return outputReady_ || normalizedInput == 0.0F;
    }

    bool GameIntegration::InstallControllerStateHook()
    {
        constexpr std::size_t managerVRSystemIndex = 1;
        constexpr std::size_t managerFakeVRSystemIndex = 3;
        constexpr std::size_t getControllerRoleIndex = 18;
        constexpr std::size_t getControllerStateIndex = 34;
        constexpr std::uint32_t rightHandRole = 2;

        const auto toolsModule = GetModuleHandleA("skyrimvrtools.dll");
        if (!toolsModule) {
            logger::warn("skyrimvrtools.dll is not loaded");
            return false;
        }

        using GetHookManager = void* (*)();
        const auto getHookManager = reinterpret_cast<GetHookManager>(
            GetProcAddress(toolsModule, "GetVRHookManager"));
        if (!getHookManager) {
            logger::warn("SkyrimVRTools does not export GetVRHookManager");
            return false;
        }

        const auto manager = static_cast<void**>(getHookManager());
        if (!manager) {
            logger::warn("SkyrimVRTools hook manager is unavailable");
            return false;
        }

        const auto realVRSystem = static_cast<void**>(manager[managerVRSystemIndex]);
        const auto fakeVRSystem = static_cast<void**>(manager[managerFakeVRSystemIndex]);
        if (!realVRSystem || !fakeVRSystem || !*realVRSystem || !*fakeVRSystem) {
            logger::warn("SkyrimVRTools OpenVR systems are not initialized");
            return false;
        }

        using GetControllerIndexForRole = std::uint32_t(void*, std::uint32_t);
        const auto realVtable = *reinterpret_cast<std::uintptr_t**>(realVRSystem);
        const auto getControllerIndexForRole =
            reinterpret_cast<GetControllerIndexForRole*>(
                realVtable[getControllerRoleIndex]);
        rightControllerIndex_ =
            getControllerIndexForRole(realVRSystem, rightHandRole);
        if (rightControllerIndex_ == UINT32_MAX) {
            logger::warn("OpenVR right controller index is invalid");
            return false;
        }

        const auto fakeVtableEntries =
            *reinterpret_cast<std::uintptr_t**>(fakeVRSystem);
        HMODULE hookOwner{};
        constexpr auto moduleFromAddress = 0x00000004;
        constexpr auto unchangedReferenceCount = 0x00000002;
        if (!GetModuleHandleExA(
                moduleFromAddress | unchangedReferenceCount,
                reinterpret_cast<LPCSTR>(
                    fakeVtableEntries[getControllerStateIndex]),
                &hookOwner) ||
            hookOwner != toolsModule) {
            logger::warn(
                "GetControllerState slot is not owned by SkyrimVRTools");
            return false;
        }

        const auto fakeVtableAddress =
            reinterpret_cast<std::uintptr_t>(fakeVtableEntries);
        REL::Relocation<std::uintptr_t> fakeVtable{ fakeVtableAddress };
        originalGetControllerState_ = fakeVtable.write_vfunc(
            getControllerStateIndex,
            GetControllerStateHook);
        if (originalGetControllerState_.address() == 0) {
            logger::warn("Unable to hook SkyrimVRTools GetControllerState");
            return false;
        }

        logger::info(
            "OpenVR output initialized for right controller index {}",
            rightControllerIndex_);
        return true;
    }

    bool GameIntegration::GetControllerStateHook(
        void* vrSystem,
        std::uint32_t deviceIndex,
        ControllerState* state,
        std::uint32_t stateSize)
    {
        auto& integration = GetSingleton();
        const auto result = integration.originalGetControllerState_(
            vrSystem,
            deviceIndex,
            state,
            stateSize);

        constexpr std::uint32_t maximumTraceCalls = 120;
        const auto traceCall =
            integration.controllerStateTraceCalls_.fetch_add(
                1,
                std::memory_order_relaxed) +
            1;
        if (traceCall <= maximumTraceCalls) {
            if (state && stateSize >= sizeof(ControllerState)) {
                logger::debug(
                    "GetControllerState call={} device={} size={} result={} "
                    "a0=({:.3f},{:.3f}) a1=({:.3f},{:.3f}) "
                    "a2=({:.3f},{:.3f}) a3=({:.3f},{:.3f}) "
                    "a4=({:.3f},{:.3f})",
                    traceCall,
                    deviceIndex,
                    stateSize,
                    result,
                    state->axes[0].x,
                    state->axes[0].y,
                    state->axes[1].x,
                    state->axes[1].y,
                    state->axes[2].x,
                    state->axes[2].y,
                    state->axes[3].x,
                    state->axes[3].y,
                    state->axes[4].x,
                    state->axes[4].y);
            } else {
                logger::debug(
                    "GetControllerState call={} device={} size={} result={} "
                    "state={}",
                    traceCall,
                    deviceIndex,
                    stateSize,
                    result,
                    state ? "present" : "null");
            }
        }

        if (result && state && stateSize >= sizeof(ControllerState)) {
            const auto hasRawAxisInput = std::ranges::any_of(
                state->axes,
                [](const ControllerAxis& axis) {
                    return std::abs(axis.x) >= 0.05F ||
                           std::abs(axis.y) >= 0.05F;
                });
            if (hasRawAxisInput) {
                constexpr std::uint32_t maximumMovingAxisLines = 80;
                const auto movingLine =
                    integration.movingAxisTraceLines_.fetch_add(
                        1,
                        std::memory_order_relaxed) +
                    1;
                if (movingLine <= maximumMovingAxisLines) {
                    logger::debug(
                        "moving raw axes line={} device={} "
                        "a0=({:.3f},{:.3f}) a1=({:.3f},{:.3f}) "
                        "a2=({:.3f},{:.3f}) a3=({:.3f},{:.3f}) "
                        "a4=({:.3f},{:.3f})",
                        movingLine,
                        deviceIndex,
                        state->axes[0].x,
                        state->axes[0].y,
                        state->axes[1].x,
                        state->axes[1].y,
                        state->axes[2].x,
                        state->axes[2].y,
                        state->axes[3].x,
                        state->axes[3].y,
                        state->axes[4].x,
                        state->axes[4].y);
                }
            }
        }

        if (!result ||
            !state ||
            stateSize < sizeof(ControllerState) ||
            deviceIndex != integration.rightControllerIndex_) {
            return result;
        }

        const auto requested = integration.requestedTurnInput_.load(
            std::memory_order_relaxed);
        const auto originalAxis = state->axes[0].x;
        state->axes[0].x = std::clamp(
            originalAxis + requested,
            -1.0F,
            1.0F);
        if (std::abs(requested) >= 0.001F) {
            constexpr std::uint32_t maximumInjectionLines = 120;
            const auto injectionLine =
                integration.injectionTraceLines_.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;
            if (injectionLine <= maximumInjectionLines) {
                logger::debug(
                    "axis injection line={} device={} requested={:.3f} "
                    "before={:.3f} after={:.3f}",
                    injectionLine,
                    deviceIndex,
                    requested,
                    originalAxis,
                    state->axes[0].x);
            }
        }
        return result;
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
