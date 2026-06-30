#pragma once

namespace HDT
{
    struct Settings
    {
        std::uint32_t schemaVersion{ 1 };
        bool enabled{ true };
        bool diagnosticOnly{ false };
        bool logPoseSamples{ true };

        float startAngle{ 15.0F };
        float stopAngle{ 11.0F };
        float maximumAngle{ 55.0F };
        float minimumTurnSpeed{ 12.0F };
        float maximumTurnSpeed{ 75.0F };
        float accelerationCurve{ 2.2F };
        float smoothingSeconds{ 0.06F };
        float minimumStickOutput{ 0.45F };
        float outputScale{ 1.0F };
        bool invertDirection{ false };

        bool pauseInMenus{ true };
        bool pauseWhenGameUnfocused{ false };
        bool pauseWhenInputBlocked{ false };
        bool pauseWhileMounted{ true };

        static Settings& GetSingleton();
        void Load();
        void Validate();
        [[nodiscard]] bool IsSchemaCompatible() const;
    };
}
