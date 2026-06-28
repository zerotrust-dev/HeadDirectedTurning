#include "TurnController.h"

namespace HDT
{
    TurnController& TurnController::GetSingleton()
    {
        static TurnController singleton;
        return singleton;
    }

    void TurnController::Start()
    {
        const auto& settings = Settings::GetSingleton();
        if (!settings.IsSchemaCompatible()) {
            logger::critical("Turn controller not started because settings are incompatible");
            return;
        }

        auto& integration = GameIntegration::GetSingleton();
        if (!integration.Initialize()) {
            logger::warn(
                "Turn controller remains inert: {}",
                integration.FailureReason());
            return;
        }

        running_ = true;
        logger::info("Turn controller started in {} mode",
            settings.diagnosticOnly ? "diagnostic" : "active");
    }

    void TurnController::Stop()
    {
        running_ = false;
        GameIntegration::GetSingleton().ApplyTurnInput(0.0F);
        turnModel_.Reset();
        smoothedTurnSpeed_ = 0.0F;
    }

    void TurnController::OnFrame(float deltaSeconds)
    {
        // Never turn to "catch up" after a breakpoint, load stall, or severe hitch.
        constexpr auto maximumSafeFrameTime = 0.1F;
        if (!running_ ||
            deltaSeconds <= 0.0F ||
            deltaSeconds > maximumSafeFrameTime) {
            GameIntegration::GetSingleton().ApplyTurnInput(0.0F);
            turnModel_.Reset();
            smoothedTurnSpeed_ = 0.0F;
            return;
        }

        auto& integration = GameIntegration::GetSingleton();
        const auto sample = integration.ReadPose();
        if (!sample) {
            return;
        }

        const auto& settings = Settings::GetSingleton();
        const auto pauseReason = GetPauseReason();
        logAccumulator_ += deltaSeconds;
        const auto logSample =
            settings.logPoseSamples && logAccumulator_ >= 0.25F;
        if (logSample) {
            logger::debug(
                "raw pose hmd={:.2f} room={:.2f} relative={:.2f} "
                "focused={} pause={}",
                sample->hmdYawDegrees,
                sample->bodyYawDegrees,
                sample->relativeYawDegrees,
                integration.IsGameFocused(),
                pauseReason);
            logAccumulator_ = 0.0F;
        }

        if (pauseReason[0] != '\0') {
            integration.ApplyTurnInput(0.0F);
            turnModel_.Reset();
            smoothedTurnSpeed_ = 0.0F;
            return;
        }

        const TurnParameters parameters{
            settings.startAngle,
            settings.stopAngle,
            settings.maximumAngle,
            settings.minimumTurnSpeed,
            settings.maximumTurnSpeed,
            settings.accelerationCurve
        };
        const auto targetSpeed = turnModel_.Calculate(sample->relativeYawDegrees, parameters);
        if (targetSpeed == 0.0F || settings.smoothingSeconds == 0.0F) {
            smoothedTurnSpeed_ = targetSpeed;
        } else {
            const auto blend = 1.0F - std::exp(-deltaSeconds / settings.smoothingSeconds);
            smoothedTurnSpeed_ += (targetSpeed - smoothedTurnSpeed_) * blend;
        }

        if (!settings.diagnosticOnly) {
            const auto direction = settings.invertDirection ? -1.0F : 1.0F;
            auto normalizedInput = 0.0F;
            if (smoothedTurnSpeed_ != 0.0F &&
                settings.maximumTurnSpeed > 0.0F) {
                const auto speedFraction = std::clamp(
                    std::abs(smoothedTurnSpeed_) /
                        settings.maximumTurnSpeed,
                    0.0F,
                    1.0F);
                const auto stickMagnitude =
                    settings.minimumStickOutput +
                    (settings.outputScale -
                        settings.minimumStickOutput) *
                        speedFraction;
                normalizedInput =
                    direction *
                    std::copysign(stickMagnitude, smoothedTurnSpeed_);
            }
            if (logSample && std::abs(normalizedInput) >= 0.001F) {
                logger::debug(
                    "turn output relative={:.2f} targetSpeed={:.2f} "
                    "smoothedSpeed={:.2f} requested={:.3f}",
                    sample->relativeYawDegrees,
                    targetSpeed,
                    smoothedTurnSpeed_,
                    normalizedInput);
            }
            if (!integration.ApplyTurnInput(normalizedInput)) {
                logger::error("Rotation output failed; stopping controller");
                Stop();
            }
        } else {
            integration.ApplyTurnInput(0.0F);
        }
    }

    const char* TurnController::GetPauseReason() const
    {
        const auto& settings = Settings::GetSingleton();
        if (!settings.enabled) {
            return "disabled";
        }

        const auto& integration = GameIntegration::GetSingleton();
        if (!integration.IsReady()) {
            return "integration-not-ready";
        }

        if (settings.pauseWhenGameUnfocused && !integration.IsGameFocused()) {
            return "unfocused";
        }

        if (settings.pauseInMenus) {
            const auto ui = RE::UI::GetSingleton();
            if (ui && ui->GameIsPaused()) {
                return "menu";
            }
        }

        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return "no-player";
        }

        const auto controls = RE::PlayerControls::GetSingleton();
        if (!controls) {
            return "no-controls";
        }
        if (controls->blockPlayerInput) {
            return "blocked-input";
        }

        if (settings.pauseWhileMounted && player->IsOnMount()) {
            return "mounted";
        }

        return "";
    }
}
