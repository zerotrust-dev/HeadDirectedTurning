#pragma once

#include "GameIntegration.h"
#include "Settings.h"
#include "TurnModel.h"

namespace HDT
{
    class TurnController
    {
    public:
        static TurnController& GetSingleton();

        void Start();
        void Stop();
        void OnFrame(float deltaSeconds);

    private:
        [[nodiscard]] const char* GetPauseReason() const;

        std::atomic_bool running_{ false };
        TurnModel turnModel_;
        float smoothedTurnSpeed_{ 0.0F };
        float logAccumulator_{ 0.0F };
    };
}
