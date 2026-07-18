cmake_minimum_required(VERSION 3.15)
project(OrchSynth VERSION 2.0.0 LANGUAGES CXX C)

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
    src/plugin/OrchVstGuiEditor.h
    src/plugin/OrchVstGuiEditor.cpp
    src/plugin/OrchFaustEntry.cpp
    src/dsp/PerformanceEvent.h
    src/dsp/FaustEngine.h
    src/dsp/FaustEngine.cpp
    src/dsp/ConvolutionProcessor.h
    src/dsp/ConvolutionProcessor.cpp
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
    third_party/FFTConvolver/AudioFFT.h
    third_party/FFTConvolver/AudioFFT.cpp
    third_party/FFTConvolver/FFTConvolver.h
    third_party/FFTConvolver/FFTConvolver.cpp
    third_party/FFTConvolver/Utilities.h
    third_party/FFTConvolver/Utilities.cpp
)

if(WIN32)
    list(APPEND SOURCE_FILES ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp)
elseif(APPLE)
    list(APPEND SOURCE_FILES ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
elseif(UNIX)
    list(APPEND SOURCE_FILES ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()

# VST3 Plugin Definition using Steinberg's macro
smtg_add_vst3plugin(OrchSynth
    ${SOURCE_FILES}
)

# Link dependencies
if(WIN32)
    target_link_libraries(OrchSynth
        PRIVATE
            sdk
            vstgui_support
            oscpack
            nlohmann_json::nlohmann_json
            faust
            ws2_32
            winmm
    )
else()
    target_link_libraries(OrchSynth
        PRIVATE
            sdk
            vstgui_support
            oscpack
            nlohmann_json::nlohmann_json
            faust
    )
endif()

# Include directories
target_include_directories(OrchSynth
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FFTConvolver
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs
        ${FAUST_DIR}/include
)

# Copy Faust DLL and libraries to build directory PRE_BUILD (Windows only)
if(WIN32)
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt"
            "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/THIRD_PARTY_LICENSES.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
            "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt"
            "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/THIRD_PARTY_LICENSES.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
            "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/gui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/gui"
            "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/gui"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/gui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/gui"
            "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/gui"
        # Copy only .lib files from Faust standard library
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/bin/Debug/faust"
        COMMAND powershell -Command "Get-ChildItem '${FAUST_DIR}/share/faust/*.lib' | ForEach-Object { Copy-Item $_.FullName '${CMAKE_BINARY_DIR}/bin/Debug/faust/' }"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/bin/Release/faust"
        COMMAND powershell -Command "Get-ChildItem '${FAUST_DIR}/share/faust/*.lib' | ForEach-Object { Copy-Item $_.FullName '${CMAKE_BINARY_DIR}/bin/Release/faust/' }"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/faust"
        COMMAND powershell -Command "Get-ChildItem '${FAUST_DIR}/share/faust/*.lib' | ForEach-Object { Copy-Item $_.FullName '${CMAKE_BINARY_DIR}/VST3/Debug/OrchSynth.vst3/Contents/x86_64-win/faust/' }"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/faust"
        COMMAND powershell -Command "Get-ChildItem '${FAUST_DIR}/share/faust/*.lib' | ForEach-Object { Copy-Item $_.FullName '${CMAKE_BINARY_DIR}/VST3/Release/OrchSynth.vst3/Contents/x86_64-win/faust/' }"
        COMMENT "Copying faust.dll and libraries to build and VST3 folders"
    )
endif()

# Copy Faust standard libraries into the VST3 bundle for macOS POST_BUILD
if(APPLE)
    add_custom_command(TARGET OrchSynth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:OrchSynth>/faust"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FAUST_DIR}/share/faust"
            "$<TARGET_FILE_DIR:OrchSynth>/faust"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt"
            "$<TARGET_FILE_DIR:OrchSynth>/../THIRD_PARTY_LICENSES.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_FILE_DIR:OrchSynth>/../LICENSE.txt"
        COMMENT "Copying Faust standard libraries and licenses into macOS VST3 bundle"
    )
endif()

# Add standalone test executable
add_executable(test_faust
    src/util/test_faust.cpp
    src/dsp/FaustEngine.cpp
    src/dsp/Voice.cpp
    src/dsp/VoiceManager.cpp
    src/dsp/ConvolutionProcessor.cpp
    src/graph/GraphParser.cpp
    src/graph/FaustGraphCompiler.cpp
    third_party/FFTConvolver/AudioFFT.cpp
    third_party/FFTConvolver/FFTConvolver.cpp
    third_party/FFTConvolver/Utilities.cpp
)
target_link_libraries(test_faust
    PRIVATE
        faust
        nlohmann_json::nlohmann_json
)
target_include_directories(test_faust
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FFTConvolver
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs
        ${FAUST_DIR}/include
)
add_custom_command(TARGET test_faust POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:test_faust>/faust
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${FAUST_DIR}/share/faust"
        $<TARGET_FILE_DIR:test_faust>/faust
)
if(WIN32)
    add_custom_command(TARGET test_faust POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FAUST_DIR}/lib/faust.dll"
            $<TARGET_FILE_DIR:test_faust>
    )
endif()
