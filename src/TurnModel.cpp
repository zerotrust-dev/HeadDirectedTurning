#include "TurnModel.h"

#include <algorithm>
#include <cmath>

namespace HDT
{
    float TurnModel::Calculate(float relativeYawDegrees, const TurnParameters& parameters)
    {
        const auto magnitude = std::abs(relativeYawDegrees);

        if (turning_) {
            turning_ = magnitude > parameters.stopAngle;
        } else {
            turning_ = magnitude >= parameters.startAngle;
        }

        if (!turning_) {
            return 0.0F;
        }

        const auto range = parameters.maximumAngle - parameters.startAngle;
        const auto normalized =
            std::clamp((magnitude - parameters.startAngle) / range, 0.0F, 1.0F);
        const auto curved = std::pow(normalized, parameters.accelerationCurve);
        const auto speed =
            std::lerp(parameters.minimumTurnSpeed, parameters.maximumTurnSpeed, curved);
        return std::copysign(speed, relativeYawDegrees);
    }

    void TurnModel::Reset()
    {
        turning_ = false;
    }
}
