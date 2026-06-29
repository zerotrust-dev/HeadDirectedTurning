cmake_minimum_required(VERSION 3.24)

project(
    HeadDirectedTurning
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "rclae and Codex"
    NAME "Head Directed Turning"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY VR
)

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        src/main.cpp
        src/GameIntegration.cpp
        src/GameIntegration.h
        src/PoseMath.cpp
        src/PoseMath.h
        src/Settings.cpp
        src/Settings.h
        src/TurnController.cpp
        src/TurnController.h
        src/TurnModel.cpp
        src/TurnModel.h
        src/Version.h
        src/PCH.h
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE HDT_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc)
endif()

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION SKSE/Plugins
)

install(
    FILES config/HeadDirectedTurning.ini
    DESTINATION SKSE/Plugins
)

install(
    FILES vendor/vigemclient/x64/ViGEmClient.dll
    DESTINATION SKSE/Plugins
)

install(
    FILES vendor/vigemclient/NOTICE.txt
    DESTINATION SKSE/Plugins
)

include(CTest)

if(BUILD_TESTING)
    add_executable(
        HeadDirectedTurningTests
        tests/TurnModelTests.cpp
        src/PoseMath.cpp
        src/PoseMath.h
        src/TurnModel.cpp
        src/TurnModel.h
    )
    target_include_directories(HeadDirectedTurningTests PRIVATE src)
    target_compile_features(HeadDirectedTurningTests PRIVATE cxx_std_23)
    add_test(NAME HeadDirectedTurningTests COMMAND HeadDirectedTurningTests)
endif()
