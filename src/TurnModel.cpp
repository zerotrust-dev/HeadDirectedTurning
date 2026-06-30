#include "TurnModel.h"

#include <algorithm>
#include <cmath>

namespace HDT
{
    float CalculateStickMagnitude(
        float smoothedTurnSpeed,
        float minimumTurnSpeed,
        float maximumTurnSpeed,
        float minimumStickOutput,
        float maximumStickOutput)
    {
        if (smoothedTurnSpeed == 0.0F) {
            return 0.0F;
        }

        const auto speedRange = maximumTurnSpeed - minimumTurnSpeed;
        const auto speedFraction = speedRange > 0.0F ?
            std::clamp(
                (std::abs(smoothedTurnSpeed) - minimumTurnSpeed) /
                    speedRange,
                0.0F,
                1.0F) :
            0.0F;
        return std::lerp(
            minimumStickOutput,
            maximumStickOutput,
            speedFraction);
    }

    float TurnModel::Calculate(float relativeYawDegrees, const TurnParameters& parameters)
    {
        const auto magnitude = std::abs(relativeYawDegrees);

        if (latchedDirection_ != 0.0F) {
            if (magnitude <= parameters.stopAngle) {
                latchedDirection_ = 0.0F;
                latchedMagnitude_ = 0.0F;
                return 0.0F;
            }
        } else if (magnitude > parameters.startAngle) {
            latchedDirection_ = std::copysign(1.0F, relativeYawDegrees);
            latchedMagnitude_ = magnitude;
        }

        if (latchedDirection_ == 0.0F) {
            return 0.0F;
        }

        // A non-neutral sample on the opposite side is feedback from Skyrim's
        // rotating room reference, not a valid direction change. Retain the
        // last same-direction magnitude so the false sample cannot spike speed.
        if (std::signbit(relativeYawDegrees) ==
            std::signbit(latchedDirection_)) {
            latchedMagnitude_ = magnitude;
        }

        const auto range = parameters.maximumAngle - parameters.startAngle;
        const auto normalized =
            std::clamp(
                (latchedMagnitude_ - parameters.startAngle) / range,
                0.0F,
                1.0F);
        const auto curved = std::pow(normalized, parameters.accelerationCurve);
        const auto speed =
            std::lerp(parameters.minimumTurnSpeed, parameters.maximumTurnSpeed, curved);
        return speed * latchedDirection_;
    }

    void TurnModel::Reset()
    {
        latchedDirection_ = 0.0F;
        latchedMagnitude_ = 0.0F;
    }
}
