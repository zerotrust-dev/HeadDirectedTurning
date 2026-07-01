#pragma once

namespace HDT
{
    struct GazeAlignmentParameters
    {
        float startAngle{};
        float stopOnReturnDegrees{};
        float alignmentTolerance{};
    };

    class GazeAlignmentModel
    {
    public:
        enum class Phase
        {
            idle,
            aligning,
            clutch
        };

        // Returns signed body-heading error in degrees. The caller converts
        // that error to a temporary right-stick command.
        [[nodiscard]] float Calculate(
            float headYawDegrees,
            float bodyYawDegrees,
            const GazeAlignmentParameters& parameters);
        void Reset();

        [[nodiscard]] Phase GetPhase() const;
        [[nodiscard]] float GetTargetBodyYaw() const;
        [[nodiscard]] float GetBodyError() const;
        [[nodiscard]] float GetHeadAnchor() const;
        [[nodiscard]] float GetClutchHeadYaw() const;

    private:
        void BeginAlignment(
            float headYawDegrees,
            float bodyYawDegrees,
            float headAnchorDegrees);
        void EnterClutch(
            float headYawDegrees,
            float bodyYawDegrees);

        Phase phase_{ Phase::idle };
        float direction_{ 0.0F };
        float headAnchorDegrees_{ 0.0F };
        float bodyAnchorDegrees_{ 0.0F };
        float peakHeadMagnitude_{ 0.0F };
        float clutchHeadYawDegrees_{ 0.0F };
        float clutchBodyYawDegrees_{ 0.0F };
        float targetBodyYawDegrees_{ 0.0F };
        float bodyErrorDegrees_{ 0.0F };
    };
}
