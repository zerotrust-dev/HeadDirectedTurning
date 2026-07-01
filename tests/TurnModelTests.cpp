#include "PoseMath.h"
#include "TurnModel.h"

#include <cassert>
#include <cmath>
#include <numbers>

namespace
{
    constexpr HDT::TurnParameters parameters{
        15.0F,
        11.0F,
        55.0F,
        12.0F,
        75.0F,
        2.2F,
        100.0F
    };

    bool Near(float actual, float expected, float tolerance = 0.001F)
    {
        return std::abs(actual - expected) <= tolerance;
    }
}

int main()
{
    assert(Near(HDT::RadiansToDegrees(std::numbers::pi_v<float>), 180.0F));
    assert(Near(HDT::NormalizeDegrees(361.0F), 1.0F));
    assert(Near(HDT::NormalizeDegrees(-361.0F), -1.0F));
    assert(Near(HDT::NormalizeDegrees(180.0F), 180.0F));
    assert(Near(HDT::NormalizeDegrees(-180.0F), 180.0F));
    assert(Near(HDT::NormalizeDegrees(540.0F), 180.0F));
    assert(Near(HDT::ProjectedYawDegrees(1.0F, 0.0F), 0.0F));
    assert(Near(HDT::ProjectedYawDegrees(0.0F, 1.0F), 90.0F));
    assert(Near(HDT::ProjectedYawDegrees(0.0F, -1.0F), -90.0F));
    assert(Near(HDT::ProjectedYawDegrees(-1.0F, 0.0F), 180.0F));
    assert(Near(HDT::ProjectedYawDegrees(0.0F, 0.0F), 0.0F));
    assert(Near(HDT::OpenVRProjectedYawDegrees(0.0F, 1.0F), 0.0F));
    // Device forward-axis X is positive while physically looking left.
    assert(Near(HDT::OpenVRProjectedYawDegrees(1.0F, 0.0F), -90.0F));
    assert(Near(HDT::OpenVRProjectedYawDegrees(-1.0F, 0.0F), 90.0F));
    assert(Near(
        HDT::CalculateStickMagnitude(
            0.0F, 12.0F, 75.0F, 0.45F, 1.0F),
        0.0F));
    assert(Near(
        HDT::CalculateStickMagnitude(
            12.0F, 12.0F, 75.0F, 0.45F, 1.0F),
        0.45F));
    assert(Near(
        HDT::CalculateStickMagnitude(
            -12.0F, 12.0F, 75.0F, 0.45F, 1.0F),
        0.45F));
    assert(Near(
        HDT::CalculateStickMagnitude(
            75.0F, 12.0F, 75.0F, 0.45F, 1.0F),
        1.0F));
    assert(Near(
        HDT::CalculateStickMagnitude(
            100.0F, 12.0F, 75.0F, 0.45F, 1.0F),
        1.0F));

    HDT::TurnModel model;

    assert(Near(model.Calculate(14.9F, parameters), 0.0F));
    assert(Near(model.Calculate(15.0F, parameters), 0.0F));
    assert(Near(model.Calculate(15.01F, parameters), 12.0F));
    assert(model.Calculate(30.0F, parameters) > 12.0F);
    assert(Near(model.Calculate(55.0F, parameters), 75.0F));

    // Direction stays latched even if Skyrim's rotating room reference makes
    // the measured yaw jump to the opposite side.
    assert(Near(model.Calculate(-55.0F, parameters), 75.0F));
    assert(Near(model.Calculate(-30.0F, parameters), 75.0F));

    // Hysteresis keeps turning active until the smaller stop threshold is crossed.
    assert(model.Calculate(12.0F, parameters) > 0.0F);
    assert(Near(model.Calculate(11.0F, parameters), 0.0F));

    // Crossing neutral unlocks direction so a genuine opposite turn can start.
    assert(Near(model.Calculate(-15.0F, parameters), -12.0F));
    assert(Near(model.Calculate(55.0F, parameters), -75.0F));
    assert(Near(model.Calculate(0.0F, parameters), 0.0F));

    model.Reset();
    assert(Near(model.Calculate(12.0F, parameters), 0.0F));
    assert(Near(model.Calculate(-55.0F, parameters), -75.0F));
    model.Reset();

    // Regression for the observed feedback sequence: no output reversal until
    // a real neutral sample arrives.
    const auto stablePositiveSpeed =
        model.Calculate(41.88F, parameters);
    assert(stablePositiveSpeed > 0.0F);
    assert(Near(
        model.Calculate(-51.73F, parameters),
        stablePositiveSpeed));
    assert(Near(
        model.Calculate(41.88F, parameters),
        stablePositiveSpeed));
    assert(Near(
        model.Calculate(-50.68F, parameters),
        stablePositiveSpeed));
    assert(Near(model.Calculate(5.99F, parameters), 0.0F));
    assert(model.Calculate(-31.38F, parameters) < 0.0F);

    // With StopAngle equal to StartAngle, the complete +/-15 degree free-look
    // zone stops output immediately after a turn has started.
    constexpr HDT::TurnParameters fullDeadzoneStopParameters{
        15.0F,
        15.0F,
        55.0F,
        12.0F,
        75.0F,
        2.2F,
        100.0F
    };
    model.Reset();
    assert(model.Calculate(20.0F, fullDeadzoneStopParameters) > 0.0F);
    assert(Near(
        model.Calculate(15.0F, fullDeadzoneStopParameters),
        0.0F));
    assert(Near(
        model.Calculate(-15.0F, fullDeadzoneStopParameters),
        0.0F));

    // An active turn stops after a small deliberate return gesture. It remains
    // suppressed until either neutral or an equal outward gesture re-arms it.
    constexpr HDT::TurnParameters returnGestureParameters{
        15.0F,
        15.0F,
        55.0F,
        12.0F,
        75.0F,
        2.2F,
        2.0F
    };
    model.Reset();
    assert(model.Calculate(20.0F, returnGestureParameters) > 0.0F);
    assert(model.Calculate(25.0F, returnGestureParameters) > 0.0F);
    assert(model.Calculate(23.1F, returnGestureParameters) > 0.0F);
    assert(Near(
        model.Calculate(23.0F, returnGestureParameters),
        0.0F));
    assert(
        model.GetPhase() ==
        HDT::TurnModel::Phase::suppressed);
    assert(Near(model.Calculate(24.9F, returnGestureParameters), 0.0F));
    assert(model.Calculate(25.0F, returnGestureParameters) > 0.0F);
    assert(model.GetPhase() == HDT::TurnModel::Phase::turning);
    assert(Near(model.Calculate(23.0F, returnGestureParameters), 0.0F));
    assert(Near(model.Calculate(15.0F, returnGestureParameters), 0.0F));
    assert(
        model.GetPhase() ==
        HDT::TurnModel::Phase::idle);
    assert(model.Calculate(-20.0F, returnGestureParameters) < 0.0F);

    // Repeated same-direction gestures can walk the view around a full circle:
    // outward starts, two degrees inward stops, two degrees outward re-arms.
    model.Reset();
    assert(model.Calculate(20.0F, returnGestureParameters) > 0.0F);
    assert(model.Calculate(25.0F, returnGestureParameters) > 0.0F);
    assert(Near(model.Calculate(23.0F, returnGestureParameters), 0.0F));
    assert(Near(model.Calculate(24.9F, returnGestureParameters), 0.0F));
    assert(model.Calculate(25.0F, returnGestureParameters) > 0.0F);
    assert(model.Calculate(30.0F, returnGestureParameters) > 0.0F);
    assert(Near(model.Calculate(28.0F, returnGestureParameters), 0.0F));
    assert(model.Calculate(30.0F, returnGestureParameters) > 0.0F);

    // A frame that crosses center and lands beyond the opposite threshold
    // starts the opposite turn even if no neutral sample was observed.
    assert(Near(model.Calculate(28.0F, returnGestureParameters), 0.0F));
    assert(model.Calculate(-20.0F, returnGestureParameters) < 0.0F);

    constexpr HDT::TurnParameters movingParameters{
        5.0F,
        5.0F,
        55.0F,
        12.0F,
        75.0F,
        2.2F,
        2.0F
    };
    model.Reset();
    assert(Near(model.Calculate(5.0F, movingParameters), 0.0F));
    assert(model.Calculate(5.1F, movingParameters) > 0.0F);
    assert(Near(model.Calculate(5.0F, movingParameters), 0.0F));
}
