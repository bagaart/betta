cmake_minimum_required(VERSION 3.21)
project(ble_scanner_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SIMPLEBLE_LOCAL_PATH "C:/ble_setup/ble_workspace/simpleble/simpleble/simpleble")

set(SIMPLEBLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SIMPLEBLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SIMPLEBLE_BUILD_SHARED OFF CACHE BOOL "" FORCE) # РЎС‚Р°С‚РёС‡РµСЃРєР°СЏ Р»РёРЅРєРѕРІРєР° (РЅРµ РЅСѓР¶РµРЅ simpleble.dll)

add_subdirectory(${SIMPLEBLE_LOCAL_PATH} simpleble_build)

# ==========================================================
# Mosquitto
# ==========================================================

set(MOSQUITTO_ROOT "C:/Program Files/mosquitto")

set(MOSQUITTO_INCLUDE_DIR "${MOSQUITTO_ROOT}/devel")
set(MOSQUITTO_LIBRARY "${MOSQUITTO_ROOT}/devel/mosquitto.lib")

if(NOT EXISTS "${MOSQUITTO_INCLUDE_DIR}/mosquitto.h")
    message(FATAL_ERROR "mosquitto.h not found!")
endif()

if(NOT EXISTS "${MOSQUITTO_LIBRARY}")
    message(FATAL_ERROR "mosquitto.lib not found!")
endif()

message(STATUS "Mosquitto include: ${MOSQUITTO_INCLUDE_DIR}")
message(STATUS "Mosquitto library: ${MOSQUITTO_LIBRARY}")

# ==========================================================
# 3. Наш исполняемый файл
# ==========================================================

add_executable(ble_scanner src/main.cpp)

target_include_directories(ble_scanner PRIVATE
    ${MOSQUITTO_INCLUDE_DIR}
)

target_link_libraries(ble_scanner PRIVATE
    simpleble::simpleble
    ${MOSQUITTO_LIBRARY}
)

# ==========================================================
# 4. Копирование DLL Mosquitto
# ==========================================================

if(WIN32)
    set(MOSQUITTO_DLLS
        mosquitto.dll
        mosquitto_common.dll
        cjson.dll
        pthreadVC3.dll
        libcrypto-3-x64.dll
        libssl-3-x64.dll
    )

    foreach(DLL ${MOSQUITTO_DLLS})
        add_custom_command(TARGET ble_scanner POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MOSQUITTO_ROOT}/${DLL}"
                "$<TARGET_FILE_DIR:ble_scanner>/${DLL}"
            COMMENT "Copying ${DLL}"
        )
    endforeach()
endif()