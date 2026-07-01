#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace HDT
{
    struct RuntimePoseSample
    {
        float yawDegrees{};
        float angularYawDegreesPerSecond{};
        std::int32_t trackingResult{};
        bool poseValid{};
        bool deviceConnected{};
    };

    // Minimal, read-only access to the OpenVR session Skyrim already owns.
    // This deliberately avoids VR_Init, hooks, controller APIs, and a link-time
    // dependency on openvr_api.dll.
    class OpenVRRuntimePose
    {
    public:
        [[nodiscard]] bool Initialize();
        [[nodiscard]] std::optional<RuntimePoseSample> Read() const;
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] const std::string& Status() const;

    private:
        void* system_{ nullptr };
        bool initialized_{ false };
        std::string status_{ "not initialized" };
    };
}
