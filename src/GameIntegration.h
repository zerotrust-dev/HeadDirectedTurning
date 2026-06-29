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
    //
    // Turn output is delivered through the standard XInput gamepad right stick,
    // not the OpenVR VR-controller axis. Skyrim VR turns from a connected
    // gamepad's right stick (proven on this rig by the earlier ViGEm projects),
    // and that path is independent of Pimax/OpenComposite VR-controller routing.
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
        // Matches WINAPI XInputGetState(DWORD, XINPUT_STATE*). The state pointer
        // is kept opaque here so this header stays free of <Xinput.h>; the
        // implementation casts it to XINPUT_STATE*.
        using XInputGetStateFn =
            std::uint32_t(__stdcall*)(std::uint32_t, void*);

        static void PlayerUpdateHook(RE::Actor* actor, float deltaSeconds);
        static std::uint32_t __stdcall XInputGetStateHook(
            std::uint32_t userIndex,
            void* state);
        bool InstallXInputHook();
        void UpdateAutomaticCenter(float deltaSeconds);
        [[nodiscard]] std::optional<PoseSample> ReadRawPose() const;

        REL::Relocation<PlayerUpdate*> originalPlayerUpdate_;
        XInputGetStateFn realXInputGetState_{ nullptr };
        std::atomic<float> requestedTurnInput_{ 0.0F };
        std::atomic<std::uint32_t> syntheticPacket_{ 0 };
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
