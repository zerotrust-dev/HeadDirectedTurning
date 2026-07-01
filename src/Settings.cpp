#include "Settings.h"
#include "Version.h"

namespace
{
    constexpr auto settingsPath = L"Data/SKSE/Plugins/HeadDirectedTurning.ini";
}

namespace HDT
{
    Settings& Settings::GetSingleton()
    {
        static Settings singleton;
        return singleton;
    }

    void Settings::Load()
    {
        CSimpleIniA ini;
        ini.SetUnicode();

        if (ini.LoadFile(settingsPath) < 0) {
            logger::warn("Could not read {}; using defaults", "HeadDirectedTurning.ini");
            Validate();
            return;
        }

        schemaVersion = static_cast<std::uint32_t>(
            std::max<long>(0, ini.GetLongValue("General", "SchemaVersion", 0)));

        if (schemaVersion > Version::settingsSchema) {
            enabled = false;
            diagnosticOnly = true;
            logger::critical(
                "Settings schema {} is newer than supported schema {}; plugin disabled",
                schemaVersion,
                Version::settingsSchema);
            return;
        }

        if (schemaVersion < Version::settingsSchema) {
            logger::info(
                "Loading older settings schema {} with version {} defaults",
                schemaVersion,
                Version::settingsSchema);
        }

        enabled = ini.GetBoolValue("General", "Enabled", enabled);
        diagnosticOnly = ini.GetBoolValue("General", "DiagnosticOnly", diagnosticOnly);
        logPoseSamples = ini.GetBoolValue("General", "LogPoseSamples", logPoseSamples);

        const std::string_view mode = ini.GetValue(
            "Turning",
            "TurningMode",
            "GazeAlignment");
        turningMode =
            mode == "Velocity" || mode == "velocity" ?
            TurningMode::velocity :
            TurningMode::gazeAlignment;
        startAngle = static_cast<float>(ini.GetDoubleValue("Turning", "StartAngle", startAngle));
        stopAngle = static_cast<float>(ini.GetDoubleValue("Turning", "StopAngle", stopAngle));
        movingStartAngle = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "MovingStartAngle",
                movingStartAngle));
        stopOnReturnDegrees = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "StopOnReturnDegrees",
                stopOnReturnDegrees));
        alignmentTolerance = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "AlignmentTolerance",
                alignmentTolerance));
        movementInputThreshold = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "MovementInputThreshold",
                movementInputThreshold));
        movementSpeedThreshold = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "MovementSpeedThreshold",
                movementSpeedThreshold));
        maximumAngle = static_cast<float>(ini.GetDoubleValue("Turning", "MaximumAngle", maximumAngle));
        minimumTurnSpeed = static_cast<float>(ini.GetDoubleValue("Turning", "MinimumTurnSpeed", minimumTurnSpeed));
        maximumTurnSpeed = static_cast<float>(ini.GetDoubleValue("Turning", "MaximumTurnSpeed", maximumTurnSpeed));
        accelerationCurve = static_cast<float>(ini.GetDoubleValue("Turning", "AccelerationCurve", accelerationCurve));
        smoothingSeconds = static_cast<float>(
            ini.GetDoubleValue("Turning", "SmoothingSeconds", smoothingSeconds));
        minimumStickOutput = static_cast<float>(
            ini.GetDoubleValue(
                "Turning",
                "MinimumStickOutput",
                minimumStickOutput));
        outputScale = static_cast<float>(
            ini.GetDoubleValue("Turning", "OutputScale", outputScale));
        invertDirection = ini.GetBoolValue("Turning", "InvertDirection", invertDirection);

        pauseInMenus = ini.GetBoolValue("Safety", "PauseInMenus", pauseInMenus);
        pauseWhenGameUnfocused = ini.GetBoolValue("Safety", "PauseWhenGameUnfocused", pauseWhenGameUnfocused);
        pauseWhenInputBlocked = ini.GetBoolValue("Safety", "PauseWhenInputBlocked", pauseWhenInputBlocked);
        pauseWhileMounted = ini.GetBoolValue("Safety", "PauseWhileMounted", pauseWhileMounted);

        Validate();
    }

    void Settings::Validate()
    {
        startAngle = std::clamp(startAngle, 1.0F, 89.0F);
        stopAngle = std::clamp(stopAngle, 0.0F, startAngle);
        movingStartAngle = std::clamp(
            movingStartAngle,
            1.0F,
            startAngle);
        stopOnReturnDegrees = std::clamp(
            stopOnReturnDegrees,
            0.25F,
            15.0F);
        alignmentTolerance = std::clamp(
            alignmentTolerance,
            0.25F,
            5.0F);
        movementInputThreshold = std::clamp(
            movementInputThreshold,
            0.0F,
            1.0F);
        movementSpeedThreshold = std::clamp(
            movementSpeedThreshold,
            0.0F,
            1000.0F);
        maximumAngle = std::clamp(maximumAngle, startAngle + 1.0F, 120.0F);
        minimumTurnSpeed = std::clamp(minimumTurnSpeed, 0.0F, 360.0F);
        maximumTurnSpeed = std::clamp(maximumTurnSpeed, minimumTurnSpeed, 720.0F);
        accelerationCurve = std::clamp(accelerationCurve, 0.25F, 6.0F);
        smoothingSeconds = std::clamp(smoothingSeconds, 0.0F, 1.0F);
        minimumStickOutput = std::clamp(minimumStickOutput, 0.0F, 1.0F);
        outputScale = std::clamp(
            outputScale,
            minimumStickOutput,
            1.0F);
    }

    bool Settings::IsSchemaCompatible() const
    {
        return schemaVersion <= Version::settingsSchema;
    }
}
