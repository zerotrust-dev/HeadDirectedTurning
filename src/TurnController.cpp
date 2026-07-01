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
        const auto locomoting = integration.IsLocomoting(
            settings.movementInputThreshold,
            settings.movementSpeedThreshold);
        const auto planarSpeed = integration.PlanarSpeed();
        const auto nativeMoving = integration.IsPlayerMoving();
        logAccumulator_ += deltaSeconds;
        const auto logSample =
            settings.logPoseSamples && logAccumulator_ >= 0.25F;
        if (pauseReason[0] != '\0') {
            if (logSample) {
                logger::debug(
                    "reference frame runtimeRaw={:.2f} center={:.2f} "
                    "controlYaw={:.2f} runtimeAngular={:.2f} "
                    "trackingResult={} valid={} connected={} "
                    "nodeLocal={:.2f} nodeRoomRelative={:.2f} "
                    "nodeWorld={:.2f} roomWorld={:.2f} pause={} "
                    "moving={} nativeMoving={} planarSpeed={:.2f}",
                    sample->runtimeYawDegrees,
                    sample->centerYawDegrees,
                    sample->relativeYawDegrees,
                    sample->runtimeAngularYawDegreesPerSecond,
                    sample->runtimeTrackingResult,
                    sample->runtimePoseValid,
                    sample->runtimeDeviceConnected,
                    sample->nodeLocalYawDegrees,
                    sample->nodeRoomRelativeYawDegrees,
                    sample->hmdYawDegrees,
                    sample->bodyYawDegrees,
                    pauseReason,
                    locomoting,
                    nativeMoving,
                    planarSpeed);
                logAccumulator_ = 0.0F;
            }
            integration.ApplyTurnInput(0.0F);
            turnModel_.Reset();
            smoothedTurnSpeed_ = 0.0F;
            return;
        }

        const auto effectiveStartAngle = locomoting ?
            settings.movingStartAngle :
            settings.startAngle;
        const auto effectiveStopAngle = locomoting ?
            settings.movingStartAngle :
            settings.stopAngle;
        const TurnParameters parameters{
            effectiveStartAngle,
            effectiveStopAngle,
            settings.maximumAngle,
            settings.minimumTurnSpeed,
            settings.maximumTurnSpeed,
            settings.accelerationCurve,
            settings.stopOnReturnDegrees
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
                const auto stickMagnitude = CalculateStickMagnitude(
                    smoothedTurnSpeed_,
                    settings.minimumTurnSpeed,
                    settings.maximumTurnSpeed,
                    settings.minimumStickOutput,
                    settings.outputScale);
                normalizedInput =
                    direction *
                    std::copysign(stickMagnitude, smoothedTurnSpeed_);
            }
            if (logSample) {
                logger::debug(
                    "control frame runtimeRaw={:.2f} center={:.2f} "
                    "controlYaw={:.2f} runtimeAngular={:.2f} "
                    "trackingResult={} valid={} connected={} "
                    "nodeLocal={:.2f} nodeRoomRelative={:.2f} "
                    "nodeWorld={:.2f} roomWorld={:.2f} "
                    "moving={} nativeMoving={} planarSpeed={:.2f} "
                    "startAngle={:.2f} stopAngle={:.2f} phase={} "
                    "latchedDirection={:.0f} latchedMagnitude={:.2f} "
                    "peakMagnitude={:.2f} targetSpeed={:.2f} "
                    "smoothedSpeed={:.2f} requested={:.3f} pause={}",
                    sample->runtimeYawDegrees,
                    sample->centerYawDegrees,
                    sample->relativeYawDegrees,
                    sample->runtimeAngularYawDegreesPerSecond,
                    sample->runtimeTrackingResult,
                    sample->runtimePoseValid,
                    sample->runtimeDeviceConnected,
                    sample->nodeLocalYawDegrees,
                    sample->nodeRoomRelativeYawDegrees,
                    sample->hmdYawDegrees,
                    sample->bodyYawDegrees,
                    locomoting,
                    nativeMoving,
                    planarSpeed,
                    effectiveStartAngle,
                    effectiveStopAngle,
                    static_cast<std::int32_t>(turnModel_.GetPhase()),
                    turnModel_.GetLatchedDirection(),
                    turnModel_.GetLatchedMagnitude(),
                    turnModel_.GetPeakMagnitude(),
                    targetSpeed,
                    smoothedTurnSpeed_,
                    normalizedInput,
                    pauseReason);
                logAccumulator_ = 0.0F;
            }
            if (!integration.ApplyTurnInput(normalizedInput)) {
                logger::error("Rotation output failed; stopping controller");
                Stop();
            }
        } else {
            if (logSample) {
                logger::debug(
                    "diagnostic frame runtimeRaw={:.2f} center={:.2f} "
                    "controlYaw={:.2f} runtimeAngular={:.2f} "
                    "nodeLocal={:.2f} nodeRoomRelative={:.2f} moving={} "
                    "phase={} targetSpeed={:.2f}",
                    sample->runtimeYawDegrees,
                    sample->centerYawDegrees,
                    sample->relativeYawDegrees,
                    sample->runtimeAngularYawDegreesPerSecond,
                    sample->nodeLocalYawDegrees,
                    sample->nodeRoomRelativeYawDegrees,
                    locomoting,
                    static_cast<std::int32_t>(turnModel_.GetPhase()),
                    targetSpeed);
                logAccumulator_ = 0.0F;
            }
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
        if (settings.pauseWhenInputBlocked && controls->blockPlayerInput) {
            return "blocked-input";
        }

        if (settings.pauseWhileMounted && player->IsOnMount()) {
            return "mounted";
        }

        return "";
    }
}
