cmake_minimum_required(VERSION 3.21)
project(ble_scanner_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies installed by setup.ps1 through vcpkg.
find_package(simpleble REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-mosquitto CONFIG REQUIRED)

# Support both common layouts:
#   src/main.cpp
#   main.cpp
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    set(SCANNER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    set(SCANNER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
else()
    message(FATAL_ERROR "Cannot find src/main.cpp or main.cpp in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_executable(ble_scanner "${SCANNER_SOURCE}")

target_link_libraries(ble_scanner PRIVATE
    simpleble::simpleble
    nlohmann_json::nlohmann_json
    unofficial::mosquitto::mosquitto
)

if(MSVC)
    target_compile_options(ble_scanner PRIVATE /W4 /EHsc)
    target_compile_definitions(ble_scanner PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
