#include "GameIntegration.h"

namespace HDT
{
    GameIntegration& GameIntegration::GetSingleton()
    {
        static GameIntegration singleton;
        return singleton;
    }

    bool GameIntegration::Initialize()
    {
        // Runtime-specific capability probes will be added with the pose provider.
        // Until then, fail closed even if DiagnosticOnly is accidentally disabled.
        ready_ = false;
        failureReason_ = "HMD pose provider has not been implemented";
        logger::warn("Game integration unavailable: {}", failureReason_);
        return false;
    }

    bool GameIntegration::IsReady() const
    {
        return ready_;
    }

    const std::string& GameIntegration::FailureReason() const
    {
        return failureReason_;
    }

    std::optional<PoseSample> GameIntegration::ReadPose() const
    {
        if (!ready_) {
            return std::nullopt;
        }

        return std::nullopt;
    }

    bool GameIntegration::IsGameFocused() const
    {
        return true;
    }

    bool GameIntegration::ApplyYawDelta(float)
    {
        return false;
    }
}
