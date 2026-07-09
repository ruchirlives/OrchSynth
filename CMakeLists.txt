cmake_minimum_required(VERSION 3.15)
project(OrchSynth VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)

# 1. Fetch nlohmann/json (Header-only)
message(STATUS "Fetching nlohmann/json...")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.2
)
FetchContent_MakeAvailable(json)

# 2. Fetch oscpack
message(STATUS "Fetching oscpack...")
FetchContent_Declare(
    oscpack
    GIT_REPOSITORY https://github.com/rossbencina/oscpack.git
    GIT_TAG        master
)
FetchContent_Populate(oscpack)
include_directories(${oscpack_SOURCE_DIR})
add_subdirectory(${oscpack_SOURCE_DIR} ${oscpack_BINARY_DIR})

# 3. Fetch VST3 SDK
message(STATUS "Fetching Steinberg VST3 SDK...")
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.7.9_build_61
    GIT_SUBMODULES_RECURSE ON
)
# We disable VST3 SDK target builds that we do not need to speed up compile times
set(SMTG_BUILD_ALL_VANS_AS_VST3 ON CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_HOST_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_PLUG_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_COMMON_CONTROLLER OFF CACHE BOOL "" FORCE)
set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vst3sdk)

# Setup Faust Local Search
set(FAUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libfaust")
include_directories(${FAUST_DIR}/include)
link_directories(${FAUST_DIR}/lib)

# Source files
set(SOURCE_FILES
    src/plugin/OrchFaustProcessor.h
    src/plugin/OrchFaustProcessor.cpp
    src/plugin/OrchFaustController.h
    src/plugin/OrchFaustController.cpp
    src/plugin/OrchFaustEntry.cpp
    src/dsp/FaustEngine.h
    src/dsp/FaustEngine.cpp
    src/dsp/Voice.h
    src/dsp/Voice.cpp
    src/dsp/VoiceManager.h
    src/dsp/VoiceManager.cpp
    src/graph/GraphTypes.h
    src/graph/GraphParser.h
    src/graph/GraphParser.cpp
    src/graph/FaustGraphCompiler.h
    src/graph/FaustGraphCompiler.cpp
    src/osc/OscServer.h
    src/osc/OscServer.cpp
    src/osc/OscCommandQueue.h
    src/osc/OscCommandQueue.cpp
    src/util/LockFreeQueue.h
    src/util/Logging.h
)

# VST3 Plugin Definition using Steinberg's macro
smtg_add_vst3plugin(OrchSynth
    ${SOURCE_FILES}
)

# Link dependencies
target_link_libraries(OrchSynth
    PRIVATE
        sdk
        oscpack
        nlohmann_json::nlohmann_json
        faust
        ws2_32
        winmm
)

# Include directories
target_include_directories(OrchSynth
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FAUST_DIR}/include
)

# Copy Faust DLL and libraries to build directory PRE_BUILD
add_custom_command(TARGET OrchSynth PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/bin/Debug"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAUST_DIR}/lib/faust.dll"
        "${CMAKE_BINARY_DIR}/bin/Debug/faust.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/bin/Release"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAUST_DIR}/lib/faust.dll"
        "${CMAKE_BINARY_DIR}/bin/Release/faust.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAUST_DIR}/lib/faust.dll"
        "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/faust.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAUST_DIR}/lib/faust.dll"
        "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/faust.dll"
#    COMMAND ${CMAKE_COMMAND} -E make_directory
#        "${CMAKE_BINARY_DIR}/bin/Debug/faust"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${FAUST_DIR}/share/faust"
#        "${CMAKE_BINARY_DIR}/bin/Debug/faust"
#    COMMAND ${CMAKE_COMMAND} -E make_directory
#        "${CMAKE_BINARY_DIR}/bin/Release/faust"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${FAUST_DIR}/share/faust"
#        "${CMAKE_BINARY_DIR}/bin/Release/faust"
#    COMMAND ${CMAKE_COMMAND} -E make_directory
#        "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/faust"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${FAUST_DIR}/share/faust"
#        "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/faust"
#    COMMAND ${CMAKE_COMMAND} -E make_directory
#        "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/faust"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${FAUST_DIR}/share/faust"
#        "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/faust"
    COMMENT "Copying faust.dll and libraries to build and VST3 folders"
)

# Add standalone test executable
add_executable(test_faust
    src/util/test_faust.cpp
    src/dsp/FaustEngine.cpp
    src/dsp/Voice.cpp
    src/dsp/VoiceManager.cpp
    src/graph/GraphParser.cpp
    src/graph/FaustGraphCompiler.cpp
)
target_link_libraries(test_faust
    PRIVATE
        faust
        nlohmann_json::nlohmann_json
)
target_include_directories(test_faust
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FAUST_DIR}/include
)
add_custom_command(TARGET test_faust POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAUST_DIR}/lib/faust.dll"
        $<TARGET_FILE_DIR:test_faust>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:test_faust>/faust
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${FAUST_DIR}/share/faust"
        $<TARGET_FILE_DIR:test_faust>/faust
)
