#include "GameIntegration.h"
#include "PoseMath.h"
#include "TurnController.h"

#include <Xinput.h>

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

        outputReady_ = InstallXInputHook();
        if (!outputReady_) {
            logger::warn(
                "XInput gamepad hook unavailable; diagnostic mode remains usable");
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

    bool GameIntegration::InstallXInputHook()
    {
        // Skyrim polls the gamepad through XInputGetState. We patch the main
        // module's import table entry so every poll passes through our hook,
        // which adds the head-turn value to the right stick. Matching by the
        // resolved function address avoids name/ordinal ambiguity and works for
        // whichever XInput version the game actually imports.
        static constexpr const char* candidates[] = {
            "XInput1_4.dll",
            "XInput1_3.dll",
            "XInput9_1_0.dll"
        };

        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (base == 0) {
            logger::warn("unable to resolve main module base");
            return false;
        }

        const auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        const auto ntHeaders =
            reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
        const auto& importDir =
            ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.VirtualAddress == 0) {
            logger::warn("main module has no import directory");
            return false;
        }

        for (const auto moduleName : candidates) {
            const auto xinputModule = GetModuleHandleA(moduleName);
            if (!xinputModule) {
                continue;
            }
            const auto realProc = GetProcAddress(xinputModule, "XInputGetState");
            if (!realProc) {
                continue;
            }

            auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                base + importDir.VirtualAddress);
            for (; descriptor->Name != 0; ++descriptor) {
                auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    base + descriptor->FirstThunk);
                for (; thunk->u1.Function != 0; ++thunk) {
                    if (reinterpret_cast<FARPROC>(thunk->u1.Function) != realProc) {
                        continue;
                    }

                    auto slot = &thunk->u1.Function;
                    DWORD oldProtect = 0;
                    if (!VirtualProtect(
                            slot,
                            sizeof(*slot),
                            PAGE_READWRITE,
                            &oldProtect)) {
                        logger::warn("VirtualProtect failed for the XInput IAT slot");
                        return false;
                    }
                    realXInputGetState_ =
                        reinterpret_cast<XInputGetStateFn>(realProc);
                    *slot = reinterpret_cast<std::uintptr_t>(&XInputGetStateHook);
                    VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);

                    logger::info(
                        "XInput gamepad hook installed via {} (turn -> right stick)",
                        moduleName);
                    return true;
                }
            }
        }

        logger::warn("XInputGetState import was not found in the main module");
        return false;
    }

    std::uint32_t __stdcall GameIntegration::XInputGetStateHook(
        std::uint32_t userIndex,
        void* state)
    {
        auto& integration = GetSingleton();
        auto result = integration.realXInputGetState_ ?
            integration.realXInputGetState_(userIndex, state) :
            static_cast<std::uint32_t>(ERROR_DEVICE_NOT_CONNECTED);

        // Only synthesize on the primary pad slot, and only when we have a
        // valid buffer to write into.
        if (!state || userIndex != 0) {
            return result;
        }

        auto inputState = static_cast<XINPUT_STATE*>(state);
        const auto requested = integration.requestedTurnInput_.load(
            std::memory_order_relaxed);

        bool modified = false;

        // Present a connected pad even when no physical/virtual gamepad exists,
        // so Skyrim reads our synthetic right stick. This mirrors the ViGEm
        // virtual pad that turned correctly on this rig.
        if (result != ERROR_SUCCESS) {
            ZeroMemory(&inputState->Gamepad, sizeof(XINPUT_GAMEPAD));
            result = ERROR_SUCCESS;
            modified = true;
        }

        if (requested != 0.0F) {
            const auto before = static_cast<int>(inputState->Gamepad.sThumbRX);
            auto value =
                before + static_cast<int>(std::lround(requested * 32767.0F));
            value = std::clamp(value, -32768, 32767);
            inputState->Gamepad.sThumbRX = static_cast<SHORT>(value);
            modified = true;

            constexpr std::uint32_t maximumInjectionLines = 120;
            const auto injectionLine =
                integration.injectionTraceLines_.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;
            if (injectionLine <= maximumInjectionLines) {
                logger::debug(
                    "xinput inject line={} user={} requested={:.3f} "
                    "beforeRX={} afterRX={}",
                    injectionLine,
                    userIndex,
                    requested,
                    before,
                    value);
            }
        }

        if (modified) {
            inputState->dwPacketNumber =
                integration.syntheticPacket_.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;
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
