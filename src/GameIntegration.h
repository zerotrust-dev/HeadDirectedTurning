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
    class GameIntegration :
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static GameIntegration& GetSingleton();

        [[nodiscard]] bool Initialize();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] const std::string& FailureReason() const;
        [[nodiscard]] std::optional<PoseSample> ReadPose() const;
        [[nodiscard]] bool IsGameFocused() const;
        [[nodiscard]] bool ApplyTurnInput(float normalizedInput);
        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* events,
            RE::BSTEventSource<RE::InputEvent*>* source) override;

    private:
        using PlayerUpdate = void(RE::Actor*, float);
        struct ControllerAxis
        {
            float x;
            float y;
        };
        struct ControllerState
        {
            std::uint32_t packetNumber;
            std::uint32_t pad04;
            std::uint64_t buttonsPressed;
            std::uint64_t buttonsTouched;
            ControllerAxis axes[5];
        };
        static_assert(sizeof(ControllerState) == 0x40);
        using GetControllerState = bool(
            void*,
            std::uint32_t,
            ControllerState*,
            std::uint32_t);

        static void PlayerUpdateHook(RE::Actor* actor, float deltaSeconds);
        static bool GetControllerStateHook(
            void* vrSystem,
            std::uint32_t deviceIndex,
            ControllerState* state,
            std::uint32_t stateSize);
        bool InstallControllerStateHook();
        void UpdateAutomaticCenter(float deltaSeconds);
        [[nodiscard]] std::optional<PoseSample> ReadRawPose() const;

        REL::Relocation<PlayerUpdate*> originalPlayerUpdate_;
        REL::Relocation<GetControllerState*> originalGetControllerState_;
        std::atomic<float> requestedTurnInput_{ 0.0F };
        std::uint32_t rightControllerIndex_{ UINT32_MAX };
        float hookLogAccumulator_{ 0.0F };
        float calibrationElapsed_{ 0.0F };
        float calibrationSinSum_{ 0.0F };
        float calibrationCosSum_{ 0.0F };
        std::uint32_t calibrationSamples_{ 0 };
        std::optional<float> centerOffsetDegrees_;
        std::uint32_t tracedThumbstickEvents_{ 0 };
        std::atomic<std::uint32_t> controllerStateTraceCalls_{ 0 };
        std::atomic<std::uint32_t> movingAxisTraceLines_{ 0 };
        bool outputReady_{ false };
        bool initialized_{ false };
        bool ready_{ false };
        std::string failureReason_;
    };
}
