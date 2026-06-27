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
        2.2F
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

    HDT::TurnModel model;

    assert(Near(model.Calculate(14.9F, parameters), 0.0F));
    assert(Near(model.Calculate(15.0F, parameters), 12.0F));
    assert(model.Calculate(30.0F, parameters) > 12.0F);
    assert(Near(model.Calculate(55.0F, parameters), 75.0F));
    assert(Near(model.Calculate(-55.0F, parameters), -75.0F));

    // Hysteresis keeps turning active until the smaller stop threshold is crossed.
    assert(model.Calculate(12.0F, parameters) > 0.0F);
    assert(Near(model.Calculate(11.0F, parameters), 0.0F));

    model.Reset();
    assert(Near(model.Calculate(12.0F, parameters), 0.0F));
}
