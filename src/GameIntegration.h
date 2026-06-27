#pragma once

#include <optional>
#include <string>

namespace HDT
{
    struct PoseSample
    {
        float hmdYawDegrees{};
        float bodyYawDegrees{};
        float relativeYawDegrees{};
    };

    // All runtime-sensitive Skyrim VR access belongs behind this boundary.
    // Keep raw offsets, hooks, VR nodes, and player rotation out of control logic.
    class GameIntegration
    {
    public:
        static GameIntegration& GetSingleton();

        [[nodiscard]] bool Initialize();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] const std::string& FailureReason() const;
        [[nodiscard]] std::optional<PoseSample> ReadPose() const;
        [[nodiscard]] bool IsGameFocused() const;
        [[nodiscard]] bool ApplyYawDelta(float degrees);

    private:
        using PlayerUpdate = void(RE::Actor*, float);

        static void PlayerUpdateHook(RE::Actor* actor, float deltaSeconds);

        REL::Relocation<PlayerUpdate*> originalPlayerUpdate_;
        float hookLogAccumulator_{ 0.0F };
        bool initialized_{ false };
        bool ready_{ false };
        std::string failureReason_;
    };
}
