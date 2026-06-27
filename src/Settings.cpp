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

        startAngle = static_cast<float>(ini.GetDoubleValue("Turning", "StartAngle", startAngle));
        stopAngle = static_cast<float>(ini.GetDoubleValue("Turning", "StopAngle", stopAngle));
        maximumAngle = static_cast<float>(ini.GetDoubleValue("Turning", "MaximumAngle", maximumAngle));
        minimumTurnSpeed = static_cast<float>(ini.GetDoubleValue("Turning", "MinimumTurnSpeed", minimumTurnSpeed));
        maximumTurnSpeed = static_cast<float>(ini.GetDoubleValue("Turning", "MaximumTurnSpeed", maximumTurnSpeed));
        accelerationCurve = static_cast<float>(ini.GetDoubleValue("Turning", "AccelerationCurve", accelerationCurve));
        smoothingSeconds = static_cast<float>(
            ini.GetDoubleValue("Turning", "SmoothingSeconds", smoothingSeconds));

        pauseInMenus = ini.GetBoolValue("Safety", "PauseInMenus", pauseInMenus);
        pauseWhenGameUnfocused = ini.GetBoolValue("Safety", "PauseWhenGameUnfocused", pauseWhenGameUnfocused);
        pauseWhileMounted = ini.GetBoolValue("Safety", "PauseWhileMounted", pauseWhileMounted);

        Validate();
    }

    void Settings::Validate()
    {
        startAngle = std::clamp(startAngle, 1.0F, 89.0F);
        stopAngle = std::clamp(stopAngle, 0.0F, startAngle);
        maximumAngle = std::clamp(maximumAngle, startAngle + 1.0F, 120.0F);
        minimumTurnSpeed = std::clamp(minimumTurnSpeed, 0.0F, 360.0F);
        maximumTurnSpeed = std::clamp(maximumTurnSpeed, minimumTurnSpeed, 720.0F);
        accelerationCurve = std::clamp(accelerationCurve, 0.25F, 6.0F);
        smoothingSeconds = std::clamp(smoothingSeconds, 0.0F, 1.0F);
    }

    bool Settings::IsSchemaCompatible() const
    {
        return schemaVersion <= Version::settingsSchema;
    }
}
