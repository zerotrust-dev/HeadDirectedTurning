#pragma once

namespace HDT
{
    struct TurnParameters
    {
        float startAngle{};
        float stopAngle{};
        float maximumAngle{};
        float minimumTurnSpeed{};
        float maximumTurnSpeed{};
        float accelerationCurve{};
    };

    class TurnModel
    {
    public:
        [[nodiscard]] float Calculate(float relativeYawDegrees, const TurnParameters& parameters);
        void Reset();

    private:
        bool turning_{ false };
    };
}
