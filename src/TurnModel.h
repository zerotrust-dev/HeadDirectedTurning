#pragma once

namespace HDT
{
    [[nodiscard]] float CalculateStickMagnitude(
        float smoothedTurnSpeed,
        float minimumTurnSpeed,
        float maximumTurnSpeed,
        float minimumStickOutput,
        float maximumStickOutput);

    struct TurnParameters
    {
        float startAngle{};
        float stopAngle{};
        float maximumAngle{};
        float minimumTurnSpeed{};
        float maximumTurnSpeed{};
        float accelerationCurve{};
        float stopOnReturnDegrees{};
    };

    class TurnModel
    {
    public:
        enum class Phase
        {
            idle,
            turning,
            suppressed
        };

        [[nodiscard]] float Calculate(float relativeYawDegrees, const TurnParameters& parameters);
        [[nodiscard]] Phase GetPhase() const;
        void Reset();

    private:
        // Direction is latched until the head returns inside stopAngle.
        // Skyrim's own rotation changes the room-space reference while turning;
        // accepting an opposite sign mid-turn creates a positive-feedback
        // left/right oscillation.
        float latchedDirection_{ 0.0F };
        float latchedMagnitude_{ 0.0F };
        float peakMagnitude_{ 0.0F };
        Phase phase_{ Phase::idle };
    };
}
