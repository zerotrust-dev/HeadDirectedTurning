#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <SimpleIni.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

using namespace std::literals;

namespace logger = SKSE::log;
