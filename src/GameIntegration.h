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
    // Turn output is delivered through a ViGEm virtual Xbox 360 controller.
    // The same mechanism (via vgamepad) drove turning correctly on this rig
    // in the trackir and phone_imu projects. From every other mod's point of
    // view the plugin is invisible: it just adds one more gamepad to Windows.
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

        static void PlayerUpdateHook(RE::Actor* actor, float deltaSeconds);
        bool InstallViGEmTarget();
        void TeardownViGEmTarget();
        void UpdateAutomaticCenter(float deltaSeconds);
        [[nodiscard]] std::optional<PoseSample> ReadRawPose() const;

        REL::Relocation<PlayerUpdate*> originalPlayerUpdate_;
        // PVIGEM_CLIENT / PVIGEM_TARGET kept opaque so this header doesn't
        // pull in the ViGEmClient headers; the .cpp casts them.
        void* vigemClient_{ nullptr };
        void* vigemTarget_{ nullptr };
        std::atomic<bool> vigemFailureLogged_{ false };
        float hookLogAccumulator_{ 0.0F };
        float calibrationElapsed_{ 0.0F };
        float calibrationSinSum_{ 0.0F };
        float calibrationCosSum_{ 0.0F };
        std::uint32_t calibrationSamples_{ 0 };
        std::optional<float> centerOffsetDegrees_;
        std::uint32_t tracedThumbstickEvents_{ 0 };
        std::atomic<std::uint32_t> injectionTraceLines_{ 0 };
        bool outputReady_{ false };
        bool initialized_{ false };
        bool ready_{ false };
        std::string failureReason_;
    };
}
