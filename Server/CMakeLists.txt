cmake_minimum_required(VERSION 3.20)
project(Cypress LANGUAGES CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(FetchContent)

# Dependencies
### MinHook
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG 1e9ad1eb42db11bfcb65461f687c656612d1b555
)

### EASTL
FetchContent_Declare(
  EASTL
  GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
  GIT_TAG e323f67e5d93fdadcd44f7998069d804697b1945
)

### json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 8167d2f64122c3ef7e5a0d9cab239fdba37aa54c
)

FetchContent_MakeAvailable(
  minhook
  EASTL
  nlohmann_json
)

# Options for variants
option(CYPRESS_GW1 "Build GW1 version" OFF)
option(CYPRESS_GW2 "Build GW2 version" OFF)
option(CYPRESS_BFN "Build BFN version" OFF)

# Default fallback
if(NOT CYPRESS_GW1 AND NOT CYPRESS_GW2 AND NOT CYPRESS_BFN)
  set(CYPRESS_GW2 ON)
endif()

# Sources
file(GLOB_RECURSE SOURCES
    dllmain.cpp
    Source/*.cpp
    include/*.cpp
)

add_library(cypress SHARED ${SOURCES})

# Includes
target_include_directories(cypress PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Source
)

# Precompiled headers
target_precompile_headers(cypress PRIVATE pch.h)

# Defines
target_compile_definitions(cypress PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    GW2SERVERPROJECT_EXPORTS
    _WINDOWS
    _USRDLL
)

# Variant-specific defines + output name
if(CYPRESS_GW1)
  target_compile_definitions(cypress PRIVATE CYPRESS_GW1)
  set_target_properties(cypress PROPERTIES OUTPUT_NAME "cypress_GW1")
elseif(CYPRESS_GW2)
  target_compile_definitions(cypress PRIVATE CYPRESS_GW2)
  set_target_properties(cypress PROPERTIES OUTPUT_NAME "cypress_GW2")
elseif(CYPRESS_BFN)
  target_compile_definitions(cypress PRIVATE CYPRESS_BFN)
  set_target_properties(cypress PROPERTIES OUTPUT_NAME "cypress_BFN")
endif()

# MSVC specifics
if(MSVC)
  target_compile_options(cypress PRIVATE /W3 /MP)
endif()

# Link libraries
if(CYPRESS_GW1)
  set(LIB_VARIANT "GW1")
elseif(CYPRESS_GW2)
  set(LIB_VARIANT "GW2")
elseif(CYPRESS_BFN)
  set(LIB_VARIANT "BFN")
endif()

target_link_directories(cypress PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/$<CONFIG>_${LIB_VARIANT}
)

target_link_libraries(cypress PRIVATE
    minhook
    EASTL
    nlohmann_json
    ws2_32.lib
)

# Optimization flags
target_compile_options(cypress PRIVATE
    $<$<CONFIG:Release>:/O2>
)

# Custom step (TouchVersionInfo)
add_custom_command(
    TARGET cypress
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch
        ${CMAKE_SOURCE_DIR}/Source/Core/VersionInfo.cpp
)
