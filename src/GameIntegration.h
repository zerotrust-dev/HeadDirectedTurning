#pragma once

#include <atomic>
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
    //
    // Turn output is sent to the pre-launch companion over shared memory. The
    // companion owns the ViGEm controller so it exists before Skyrim discovers
    // input devices.
    class GameIntegration :
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static GameIntegration& GetSingleton();

        [[nodiscard]] bool Initialize();
        [[nodiscard]] bool InitializeOutput();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] const std::string& FailureReason() const;
        [[nodiscard]] std::optional<PoseSample> ReadPose() const;
        [[nodiscard]] bool IsGameFocused() const;
        [[nodiscard]] bool IsLocomoting(float inputThreshold) const;
        [[nodiscard]] bool ApplyTurnInput(float normalizedInput);
        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* events,
            RE::BSTEventSource<RE::InputEvent*>* source) override;

    private:
        using PlayerUpdate = void(RE::Actor*, float);

        ~GameIntegration();
        static void PlayerUpdateHook(RE::Actor* actor, float deltaSeconds);
        bool ConnectCompanion();
        void DisconnectCompanion();
        void UpdateAutomaticCenter(float deltaSeconds);
        [[nodiscard]] std::optional<PoseSample> ReadRawPose() const;

        REL::Relocation<PlayerUpdate*> originalPlayerUpdate_;
        void* companionMapping_{ nullptr };
        void* companionState_{ nullptr };
        std::uint64_t lastCompanionRetryMilliseconds_{ 0 };
        float hookLogAccumulator_{ 0.0F };
        float calibrationElapsed_{ 0.0F };
        float calibrationSinSum_{ 0.0F };
        float calibrationCosSum_{ 0.0F };
        std::uint32_t calibrationSamples_{ 0 };
        std::optional<float> centerOffsetDegrees_;
        std::uint32_t tracedThumbstickEvents_{ 0 };
        std::atomic<std::uint32_t> injectionTraceLines_{ 0 };
        std::atomic<float> locomotionInputMagnitude_{ 0.0F };
        std::atomic<std::uint64_t> lastLocomotionInputMilliseconds_{ 0 };
        bool outputInitialized_{ false };
        bool outputReady_{ false };
        bool initialized_{ false };
        bool ready_{ false };
        std::string failureReason_;
    };
}
