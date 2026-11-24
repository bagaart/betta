cmake_minimum_required(VERSION 3.21)
project(ble_scanner_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================================
# 1. РЈРљРђР–РРўР• Р—Р”Р•РЎР¬ РџРЈРўР¬ Рљ Р’РђРЁР•Р™ РЎРљРђР§РђРќРќРћР™ РџРђРџРљР• SIMPLEBLE
# (РСЃРїРѕР»СЊР·СѓР№С‚Рµ РїСЂСЏРјС‹Рµ СЃР»РµС€Рё /)
# ==========================================================
set(SIMPLEBLE_LOCAL_PATH "C:/ble_setup/ble_workspace/simpleble/simpleble/simpleble")

# РћС‚РєР»СЋС‡Р°РµРј СЃР±РѕСЂРєСѓ РїСЂРёРјРµСЂРѕРІ Рё С‚РµСЃС‚РѕРІ РІРЅСѓС‚СЂРё simpleble РґР»СЏ СѓСЃРєРѕСЂРµРЅРёСЏ
set(SIMPLEBLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SIMPLEBLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SIMPLEBLE_BUILD_SHARED OFF CACHE BOOL "" FORCE) # РЎС‚Р°С‚РёС‡РµСЃРєР°СЏ Р»РёРЅРєРѕРІРєР° (РЅРµ РЅСѓР¶РµРЅ simpleble.dll)

# Р”РѕР±Р°РІР»СЏРµРј Р»РѕРєР°Р»СЊРЅСѓСЋ РїР°РїРєСѓ РєР°Рє С‡Р°СЃС‚СЊ РЅР°С€РµРіРѕ РїСЂРѕРµРєС‚Р°
add_subdirectory(${SIMPLEBLE_LOCAL_PATH} simpleble_build)

# ==========================================================
# 2. РќР°СЃС‚СЂРѕР№РєРё Mosquitto
# ==========================================================
set(MOSQUITTO_INCLUDE_DIR "C:/Program Files/mosquitto/devel")
set(MOSQUITTO_LIBRARY "C:/Program Files/mosquitto/devel/mosquitto.lib")

# ==========================================================
# 3. РќР°С€ РёСЃРїРѕР»РЅСЏРµРјС‹Р№ С„Р°Р№Р»
# ==========================================================
add_executable(ble_scanner src/main.cpp)

target_include_directories(ble_scanner PRIVATE ${MOSQUITTO_INCLUDE_DIR})

target_link_libraries(ble_scanner PRIVATE 
    simpleble::simpleble 
    ${MOSQUITTO_LIBRARY}
)

if(WIN32)
    target_compile_definitions(ble_scanner PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()