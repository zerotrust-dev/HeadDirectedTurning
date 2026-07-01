#include "OpenVRRuntimePose.h"

#include "PoseMath.h"

#include <Windows.h>

#include <array>
#include <cmath>

namespace
{
    // ABI-compatible subset of OpenVR 1.x. Skyrim VR and SkyrimVRTools use
    // IVRSystem_019. GetDeviceToAbsoluteTrackingPose is vtable slot 11.
    constexpr auto openVrSystemVersion = "IVRSystem_019";
    constexpr std::size_t getDeviceToAbsoluteTrackingPoseSlot = 11;
    constexpr std::uint32_t hmdDeviceIndex = 0;
    constexpr std::uint32_t maximumTrackedDevices = 64;

    enum class TrackingUniverseOrigin : std::int32_t
    {
        seated = 0,
        standing = 1
    };

    struct Matrix34
    {
        float m[3][4];
    };

    struct Vector3
    {
        float v[3];
    };

    struct TrackedDevicePose
    {
        Matrix34 deviceToAbsoluteTracking;
        Vector3 velocity;
        Vector3 angularVelocity;
        std::int32_t trackingResult;
        bool poseIsValid;
        bool deviceIsConnected;
    };
    static_assert(sizeof(Matrix34) == 48);
    static_assert(sizeof(TrackedDevicePose) == 80);

    using GetGenericInterface = void* (__cdecl*)(
        const char* interfaceVersion,
        std::int32_t* error);
    using GetDeviceToAbsoluteTrackingPose = void(__fastcall*)(
        void* system,
        TrackingUniverseOrigin origin,
        float predictedSeconds,
        TrackedDevicePose* poses,
        std::uint32_t poseCount);
}

namespace HDT
{
    bool OpenVRRuntimePose::Initialize()
    {
        if (initialized_) {
            return system_ != nullptr;
        }
        initialized_ = true;

        const auto module = GetModuleHandleW(L"openvr_api.dll");
        if (!module) {
            status_ = "openvr_api.dll is not loaded";
            return false;
        }

        const auto getInterface = reinterpret_cast<GetGenericInterface>(
            GetProcAddress(module, "VR_GetGenericInterface"));
        if (!getInterface) {
            status_ = "VR_GetGenericInterface export is unavailable";
            return false;
        }

        std::int32_t error = 0;
        system_ = getInterface(openVrSystemVersion, &error);
        if (!system_ || error != 0) {
            system_ = nullptr;
            status_ = "IVRSystem_019 unavailable; OpenVR error " +
                std::to_string(error);
            return false;
        }

        const auto vtable = *static_cast<void***>(system_);
        if (!vtable || !vtable[getDeviceToAbsoluteTrackingPoseSlot]) {
            system_ = nullptr;
            status_ = "IVRSystem pose method is unavailable";
            return false;
        }

        status_ = "IVRSystem_019 runtime pose ready";
        return true;
    }

    std::optional<RuntimePoseSample> OpenVRRuntimePose::Read() const
    {
        if (!system_) {
            return std::nullopt;
        }

        std::array<TrackedDevicePose, maximumTrackedDevices> poses{};
        const auto vtable = *static_cast<void***>(system_);
        const auto readPoses =
            reinterpret_cast<GetDeviceToAbsoluteTrackingPose>(
                vtable[getDeviceToAbsoluteTrackingPoseSlot]);
        readPoses(
            system_,
            TrackingUniverseOrigin::standing,
            0.0F,
            poses.data(),
            static_cast<std::uint32_t>(poses.size()));

        const auto& pose = poses[hmdDeviceIndex];
        if (!pose.poseIsValid || !pose.deviceIsConnected) {
            return RuntimePoseSample{
                0.0F,
                0.0F,
                pose.trackingResult,
                pose.poseIsValid,
                pose.deviceIsConnected
            };
        }

        // OpenVR is right-handed: +Y up, +X right, -Z forward. Project the
        // forward vector onto the floor so pitch and roll cannot contaminate
        // yaw. Negation maps physical left to negative Xbox right-stick X and
        // physical right to positive, preserving InvertDirection=false.
        const auto& matrix = pose.deviceToAbsoluteTracking.m;
        const auto yaw = OpenVRProjectedYawDegrees(
            matrix[0][2],
            matrix[2][2]);
        const auto angularYaw =
            -RadiansToDegrees(pose.angularVelocity.v[1]);
        return RuntimePoseSample{
            NormalizeDegrees(yaw),
            angularYaw,
            pose.trackingResult,
            true,
            true
        };
    }

    bool OpenVRRuntimePose::IsReady() const
    {
        return system_ != nullptr;
    }

    const std::string& OpenVRRuntimePose::Status() const
    {
        return status_;
    }
}
