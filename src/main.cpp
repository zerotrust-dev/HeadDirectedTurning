#include "GameIntegration.h"
#include "Settings.h"
#include "TurnController.h"
#include "Version.h"

namespace
{
    void InitializeLogging()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Unable to locate SKSE log directory");
        }

        *path /= "HeadDirectedTurning.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));

        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            HDT::Settings::GetSingleton().Load();
            HDT::TurnController::GetSingleton().Start();
            logger::info("Data loaded; diagnostic controller initialized");
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitializeLogging();
    logger::info("{} {} loading", HDT::Version::name, HDT::Version::semantic);

    SKSE::Init(skse);

    // The companion must already own the virtual controller before Skyrim
    // starts. Connect to its shared-memory output channel as early as possible.
    (void)HDT::GameIntegration::GetSingleton().InitializeOutput();

    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::critical("Unable to register SKSE message listener");
        return false;
    }

    logger::info("Plugin loaded successfully");
    return true;
}
