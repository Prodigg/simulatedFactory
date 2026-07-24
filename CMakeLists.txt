cmake_minimum_required(VERSION 4.2)
project(simulatedFactory)

set(CMAKE_CXX_STANDARD 26)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_executable(simulatedFactory src/main.cpp
        src/Framework.h
        src/Framework.cpp
        src/AdsVariableList.h
        src/osSpecific/LinuxSpecific.h
        src/stdMachines.h
)

find_package(AdsLib REQUIRED)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
        crow
        GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
        GIT_TAG v1.3.3
)

FetchContent_MakeAvailable(crow)

target_link_libraries(simulatedFactory PRIVATE
        AdsLib::AdsLib
        nlohmann_json::nlohmann_json
        Crow::Crow
)