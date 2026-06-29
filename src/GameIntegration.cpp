#include "GameIntegration.h"
#include "CompanionProtocol.h"
#include "PoseMath.h"
#include "TurnController.h"

namespace HDT
{
    GameIntegration& GameIntegration::GetSingleton()
    {
        static GameIntegration singleton;
        return singleton;
    }

    GameIntegration::~GameIntegration()
    {
        DisconnectCompanion();
    }

    bool GameIntegration::InitializeOutput()
    {
        if (outputInitialized_) {
            return outputReady_;
        }
        outputInitialized_ = true;
        outputReady_ = ConnectCompanion();
        if (!outputReady_) {
            logger::warn(
                "companion output unavailable; start HeadDirectedTurningCompanion "
                "before Skyrim");
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

        const auto now = GetTickCount64();
        if (!outputReady_ && now - lastCompanionRetryMilliseconds_ >= 1000) {
            lastCompanionRetryMilliseconds_ = now;
            outputReady_ = ConnectCompanion();
        }
        if (!outputReady_ || !companionState_) {
            return normalizedInput == 0.0F;
        }

        auto state = static_cast<CompanionProtocol::State*>(companionState_);
        const auto scaled = std::lround(normalizedInput * 32767.0F);
        const auto stickRX = static_cast<LONG>(
            std::clamp<long>(scaled, -32768L, 32767L));
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&state->stickRX),
            stickRX);
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(
                &state->heartbeatMilliseconds),
            static_cast<LONG64>(now));
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&state->sequence));

        if (std::abs(normalizedInput) >= 0.001F) {
            constexpr std::uint32_t maximumInjectionLines = 120;
            const auto line =
                injectionTraceLines_.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;
            if (line <= maximumInjectionLines) {
                logger::debug(
                    "companion output line={} requested={:.3f} sThumbRX={}",
                    line,
                    normalizedInput,
                    stickRX);
            }
        }
        return true;
    }

    bool GameIntegration::ConnectCompanion()
    {
        if (companionMapping_ && companionState_) {
            return true;
        }

        const auto mapping = OpenFileMappingW(
            FILE_MAP_READ | FILE_MAP_WRITE,
            FALSE,
            CompanionProtocol::mappingName);
        if (!mapping) {
            return false;
        }

        const auto state = static_cast<CompanionProtocol::State*>(
            MapViewOfFile(
                mapping,
                FILE_MAP_READ | FILE_MAP_WRITE,
                0,
                0,
                sizeof(CompanionProtocol::State)));
        if (!state) {
            CloseHandle(mapping);
            return false;
        }

        if (state->magicValue != CompanionProtocol::magic ||
            state->versionValue != CompanionProtocol::version) {
            logger::error(
                "companion protocol mismatch: magic=0x{:08x} version={}",
                state->magicValue,
                state->versionValue);
            UnmapViewOfFile(state);
            CloseHandle(mapping);
            return false;
        }

        companionMapping_ = mapping;
        companionState_ = state;
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&state->pluginProcessId),
            static_cast<LONG>(GetCurrentProcessId()));
        logger::info("connected to pre-launch companion output channel");
        return true;
    }

    void GameIntegration::DisconnectCompanion()
    {
        if (companionState_) {
            auto state = static_cast<CompanionProtocol::State*>(companionState_);
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(&state->stickRX),
                0);
            InterlockedExchange64(
                reinterpret_cast<volatile LONG64*>(
                    &state->heartbeatMilliseconds),
                0);
            UnmapViewOfFile(state);
            companionState_ = nullptr;
        }
        if (companionMapping_) {
            CloseHandle(companionMapping_);
            companionMapping_ = nullptr;
        }
        outputReady_ = false;
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
            if (integration.companionState_) {
                const auto state =
                    static_cast<CompanionProtocol::State*>(
                        integration.companionState_);
                const auto requested = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(&state->stickRX),
                    0,
                    0);
                const auto sequence = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(&state->sequence),
                    0,
                    0);
                const auto applied = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->appliedStickRX),
                    0,
                    0);
                const auto appliedSequence = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->appliedSequence),
                    0,
                    0);
                const auto error = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->lastViGEmError),
                    0,
                    0);
                const auto companionHeartbeat =
                    static_cast<std::uint64_t>(InterlockedCompareExchange64(
                        reinterpret_cast<volatile LONG64*>(
                            &state->companionHeartbeatMilliseconds),
                        0,
                        0));
                const auto age = GetTickCount64() >= companionHeartbeat ?
                    GetTickCount64() - companionHeartbeat :
                    0;
                const auto userIndex = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->vigemUserIndex),
                    0,
                    0);
                const auto slotMask = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->connectedXInputSlots),
                    0,
                    0);
                const auto companionPid = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->companionProcessId),
                    0,
                    0);
                const auto watchdog = InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(
                        &state->watchdogActive),
                    0,
                    0);
                logger::debug(
                    "companion status requestedRX={} seq={} appliedRX={} "
                    "appliedSeq={} vigemError=0x{:08x} heartbeatAge={}ms "
                    "userIndex={} xinputSlots=0x{:x} companionPid={} "
                    "watchdog={}",
                    requested,
                    sequence,
                    applied,
                    appliedSequence,
                    static_cast<std::uint32_t>(error),
                    age,
                    userIndex,
                    static_cast<std::uint32_t>(slotMask),
                    companionPid,
                    watchdog);
            }
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
