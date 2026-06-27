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
        logAccumulator_ += deltaSeconds;
        if (settings.logPoseSamples && logAccumulator_ >= 0.25F) {
            logger::debug(
                "raw pose hmd={:.2f} body={:.2f} relative={:.2f} focused={}",
                sample->hmdYawDegrees,
                sample->bodyYawDegrees,
                sample->relativeYawDegrees,
                integration.IsGameFocused());
            logAccumulator_ = 0.0F;
        }

        if (ShouldPause()) {
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
            const auto yawDelta = smoothedTurnSpeed_ * deltaSeconds;
            if (!integration.ApplyYawDelta(yawDelta)) {
                logger::error("Rotation output failed; stopping controller");
                Stop();
            }
        }
    }

    bool TurnController::ShouldPause() const
    {
        const auto& settings = Settings::GetSingleton();
        if (!settings.enabled) {
            return true;
        }

        const auto& integration = GameIntegration::GetSingleton();
        if (!integration.IsReady()) {
            return true;
        }

        if (settings.pauseWhenGameUnfocused && !integration.IsGameFocused()) {
            return true;
        }

        if (settings.pauseInMenus) {
            const auto ui = RE::UI::GetSingleton();
            if (ui && ui->GameIsPaused()) {
                return true;
            }
        }

        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return true;
        }

        if (settings.pauseWhileMounted && player->IsOnMount()) {
            return true;
        }

        return false;
    }
}
