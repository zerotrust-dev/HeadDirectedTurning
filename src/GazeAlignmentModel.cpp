#include "GazeAlignmentModel.h"

#include "PoseMath.h"

#include <algorithm>
#include <cmath>

namespace HDT
{
    float GazeAlignmentModel::Calculate(
        float headYawDegrees,
        float bodyYawDegrees,
        const GazeAlignmentParameters& parameters)
    {
        const auto headMagnitude = std::abs(headYawDegrees);

        if (phase_ == Phase::clutch) {
            if (headMagnitude <= parameters.startAngle) {
                Reset();
                return 0.0F;
            }

            const auto movementFromClutch = NormalizeDegrees(
                headYawDegrees - clutchHeadYawDegrees_);
            const auto movementDirection =
                std::copysign(1.0F, movementFromClutch);
            const auto outwardGesture =
                movementDirection == direction_ &&
                std::abs(movementFromClutch) >=
                    parameters.stopOnReturnDegrees;
            const auto crossedToOppositeSide =
                std::copysign(1.0F, headYawDegrees) != direction_ &&
                headMagnitude > parameters.startAngle;

            if (outwardGesture) {
                BeginAlignment(
                    headYawDegrees,
                    bodyYawDegrees,
                    clutchHeadYawDegrees_);
            } else if (crossedToOppositeSide) {
                BeginAlignment(headYawDegrees, bodyYawDegrees, 0.0F);
            } else {
                return 0.0F;
            }
        }

        if (phase_ == Phase::idle) {
            if (headMagnitude <= parameters.startAngle) {
                return 0.0F;
            }
            BeginAlignment(headYawDegrees, bodyYawDegrees, 0.0F);
        }

        const auto headDelta = NormalizeDegrees(
            headYawDegrees - headAnchorDegrees_);
        const auto headDeltaMagnitude = std::abs(headDelta);
        const auto currentDirection =
            std::copysign(1.0F, headDelta);

        if (currentDirection == direction_) {
            if (peakHeadMagnitude_ - headDeltaMagnitude >=
                parameters.stopOnReturnDegrees) {
                EnterClutch(headYawDegrees, bodyYawDegrees);
                return 0.0F;
            }
            peakHeadMagnitude_ = (std::max)(
                peakHeadMagnitude_,
                headDeltaMagnitude);
        }

        targetBodyYawDegrees_ = NormalizeDegrees(
            bodyAnchorDegrees_ + headDelta);
        bodyErrorDegrees_ = NormalizeDegrees(
            targetBodyYawDegrees_ - bodyYawDegrees);

        const auto crossedTarget =
            std::copysign(1.0F, bodyErrorDegrees_) != direction_;
        if (std::abs(bodyErrorDegrees_) <= parameters.alignmentTolerance ||
            crossedTarget) {
            EnterClutch(headYawDegrees, bodyYawDegrees);
            return 0.0F;
        }

        return bodyErrorDegrees_;
    }

    void GazeAlignmentModel::Reset()
    {
        phase_ = Phase::idle;
        direction_ = 0.0F;
        headAnchorDegrees_ = 0.0F;
        bodyAnchorDegrees_ = 0.0F;
        peakHeadMagnitude_ = 0.0F;
        clutchHeadYawDegrees_ = 0.0F;
        clutchBodyYawDegrees_ = 0.0F;
        targetBodyYawDegrees_ = 0.0F;
        bodyErrorDegrees_ = 0.0F;
    }

    GazeAlignmentModel::Phase GazeAlignmentModel::GetPhase() const
    {
        return phase_;
    }

    float GazeAlignmentModel::GetTargetBodyYaw() const
    {
        return targetBodyYawDegrees_;
    }

    float GazeAlignmentModel::GetBodyError() const
    {
        return bodyErrorDegrees_;
    }

    float GazeAlignmentModel::GetHeadAnchor() const
    {
        return headAnchorDegrees_;
    }

    float GazeAlignmentModel::GetClutchHeadYaw() const
    {
        return clutchHeadYawDegrees_;
    }

    void GazeAlignmentModel::BeginAlignment(
        float headYawDegrees,
        float bodyYawDegrees,
        float headAnchorDegrees)
    {
        phase_ = Phase::aligning;
        headAnchorDegrees_ = headAnchorDegrees;
        bodyAnchorDegrees_ = bodyYawDegrees;
        const auto headDelta = NormalizeDegrees(
            headYawDegrees - headAnchorDegrees_);
        direction_ = std::copysign(1.0F, headDelta);
        peakHeadMagnitude_ = std::abs(headDelta);
        targetBodyYawDegrees_ = NormalizeDegrees(
            bodyAnchorDegrees_ + headDelta);
        bodyErrorDegrees_ = NormalizeDegrees(
            targetBodyYawDegrees_ - bodyYawDegrees);
    }

    void GazeAlignmentModel::EnterClutch(
        float headYawDegrees,
        float bodyYawDegrees)
    {
        phase_ = Phase::clutch;
        clutchHeadYawDegrees_ = headYawDegrees;
        clutchBodyYawDegrees_ = bodyYawDegrees;
        peakHeadMagnitude_ = 0.0F;
        bodyErrorDegrees_ = 0.0F;
    }
}
