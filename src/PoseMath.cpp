#include "PoseMath.h"

#include <cmath>
#include <numbers>

namespace HDT
{
    float RadiansToDegrees(float radians)
    {
        return radians * (180.0F / std::numbers::pi_v<float>);
    }

    float NormalizeDegrees(float degrees)
    {
        if (!std::isfinite(degrees)) {
            return 0.0F;
        }

        auto normalized = std::remainder(degrees, 360.0F);
        if (normalized == -180.0F) {
            normalized = 180.0F;
        }
        return normalized;
    }

    float ProjectedYawDegrees(float xAxisX, float xAxisY)
    {
        if (!std::isfinite(xAxisX) || !std::isfinite(xAxisY) ||
            std::hypot(xAxisX, xAxisY) < 0.0001F) {
            return 0.0F;
        }
        return NormalizeDegrees(
            RadiansToDegrees(std::atan2(xAxisY, xAxisX)));
    }
}
