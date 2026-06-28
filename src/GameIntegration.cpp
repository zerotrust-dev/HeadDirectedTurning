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
        const auto inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            return false;
        }

        // Input events are engine-owned types whose constructors/destructors
        // are not exported by CommonLib. Build the exact zeroed layout and use
        // Skyrim VR's relocated vtable for this synchronous dispatch only.
        alignas(RE::ThumbstickEvent)
            std::array<std::byte, sizeof(RE::ThumbstickEvent)> eventStorage{};
        auto event = reinterpret_cast<RE::ThumbstickEvent*>(eventStorage.data());

        static REL::Relocation<std::uintptr_t> thumbstickVtable{
            RE::VTABLE_ThumbstickEvent[0]
        };
        *reinterpret_cast<std::uintptr_t*>(event) = thumbstickVtable.address();
        static const RE::BSFixedString rightStickEvent{ "Right Stick" };
        std::memcpy(
            std::addressof(event->userEvent),
            std::addressof(rightStickEvent),
            sizeof(rightStickEvent));
        event->device = RE::INPUT_DEVICE::kVRRight;
        event->eventType = RE::INPUT_EVENT_TYPE::kThumbstick;
        event->next = nullptr;
        event->idCode = RE::ThumbstickEvent::InputType::kRightThumbstick;
        event->xValue = std::clamp(normalizedInput, -1.0F, 1.0F);
        event->yValue = 0.0F;

        RE::InputEvent* eventHead = event;
        dispatchingSyntheticEvent_ = true;
        inputManager->SendEvent(&eventHead);
        dispatchingSyntheticEvent_ = false;
        return true;
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
                dispatchingSyntheticEvent_ ? "synthetic" : "physical",
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
