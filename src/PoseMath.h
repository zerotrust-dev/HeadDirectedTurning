#pragma once

namespace HDT
{
    [[nodiscard]] float RadiansToDegrees(float radians);
    [[nodiscard]] float NormalizeDegrees(float degrees);
    [[nodiscard]] float ProjectedYawDegrees(float xAxisX, float xAxisY);
    [[nodiscard]] float OpenVRProjectedYawDegrees(
        float forwardAxisX,
        float forwardAxisZ);
}
