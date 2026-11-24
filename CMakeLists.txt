cmake_minimum_required(VERSION 3.22)

project(Access808 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: read every source as UTF-8, so the non-ASCII manufacturer name in
# JUCE's generated JucePluginDefines.h (used by the VST3 wrapper target too)
# reaches the binary intact instead of being mangled by the system codepage.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The manufacturer name. Keep this file UTF-8: the c with caron must survive.
set(ACCESS808_VENDOR "Hermis Kasperavičius")

# Clang and GCC fuse a * b + c into one FMA instruction where the CPU has one
# (Apple Silicon always does), which skips a rounding step. Keep the arithmetic
# the same as the MSVC build every test was written against.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-ffp-contract=off)
endif()

# Where the SDKs live. Override on the cmake command line if they move. These
# are CACHE variables, so the first configure of a build folder freezes them.
# CI clones stock JUCE and applies patches/juce-vst3-utf16-vendor.patch.
set(ACCESS808_JUCE_DIR   "C:/_tools/JUCE"         CACHE PATH "Path to the JUCE source tree")
set(ACCESS808_CATCH2_DIR "C:/_tools/Catch2-3.5.4" CACHE PATH "Path to the Catch2 source tree")

if(NOT EXISTS "${ACCESS808_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${ACCESS808_JUCE_DIR}. Set -DACCESS808_JUCE_DIR=...")
endif()

add_subdirectory("${ACCESS808_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# VST3 everywhere, plus AU on macOS (Logic and GarageBand load only AU; JUCE
# drops AU on other platforms). BUNDLE_ID is explicit because the default is
# built from COMPANY_NAME, and the c with caron is not valid in a bundle ID.
# The codes and the bundle ID are saved in users' projects: never change them.
juce_add_plugin(Access808
    COMPANY_NAME "${ACCESS808_VENDOR}"
    COMPANY_COPYRIGHT "Copyright 2026 ${ACCESS808_VENDOR}"
    PRODUCT_NAME "Access808"
    BUNDLE_ID com.access808.access808
    PLUGIN_MANUFACTURER_CODE Acs8
    PLUGIN_CODE A808
    FORMATS VST3 AU
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

# Correct the vendor name in moduleinfo.json after JUCE writes it (see the script).
file(WRITE "${CMAKE_BINARY_DIR}/vendor.txt" "${ACCESS808_VENDOR}")
get_target_property(access808_vst3_bundle Access808_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
add_custom_command(TARGET Access808_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DMODULEINFO=${access808_vst3_bundle}/Contents/Resources/moduleinfo.json"
            "-DVENDOR_FILE=${CMAKE_BINARY_DIR}/vendor.txt"
            -P "${CMAKE_SOURCE_DIR}/cmake/FixModuleInfoVendor.cmake"
    VERBATIM)

target_sources(Access808 PRIVATE
    Source/Parameters.cpp
    Source/PresetManager.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReaperHost.cpp
    Source/ScreenReaderAnnouncer.cpp)

target_include_directories(Access808 PRIVATE Source)

target_compile_definitions(Access808 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Access808
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Unit tests for the JUCE-free DSP engine (Source/Engine808.h) ----
option(ACCESS808_BUILD_TESTS "Build the unit tests" ON)

if(ACCESS808_BUILD_TESTS AND NOT EXISTS "${ACCESS808_CATCH2_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Catch2 not found at ${ACCESS808_CATCH2_DIR}. Set -DACCESS808_CATCH2_DIR=... or -DACCESS808_BUILD_TESTS=OFF")
endif()

if(ACCESS808_BUILD_TESTS)
    add_subdirectory("${ACCESS808_CATCH2_DIR}" "${CMAKE_BINARY_DIR}/Catch2" EXCLUDE_FROM_ALL)
    add_executable(EngineTests tests/EngineTests.cpp)
    target_include_directories(EngineTests PRIVATE Source)
    target_link_libraries(EngineTests PRIVATE Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME EngineTests COMMAND EngineTests)
endif()

# ---- Offline tests of the full processor (saturation, ducking, width, state) ----
if(ACCESS808_BUILD_TESTS)
    add_executable(ProcessorTests tests/ProcessorTests.cpp)
    target_link_libraries(ProcessorTests PRIVATE Access808 Catch2::Catch2)
    # The JUCE module code is compiled into the Access808 shared-code library; reuse its include paths and defines.
    target_include_directories(ProcessorTests PRIVATE Source $<TARGET_PROPERTY:Access808,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ProcessorTests PRIVATE $<TARGET_PROPERTY:Access808,COMPILE_DEFINITIONS>)
    add_test(NAME ProcessorTests COMMAND ProcessorTests)
endif()

# ---- Installer (Inno Setup 6): cmake --build build --config Release --target installer ----
find_program(ISCC_EXECUTABLE ISCC PATHS "C:/Program Files (x86)/Inno Setup 6" "C:/Program Files/Inno Setup 6")
if(ISCC_EXECUTABLE)
    add_custom_target(installer
        COMMAND "${ISCC_EXECUTABLE}" /Q
                "/DAppVersion=${PROJECT_VERSION}"
                "/DVst3Dir=${CMAKE_BINARY_DIR}/Access808_artefacts/$<CONFIG>/VST3/Access808.vst3"
                "/DOutputDir=${CMAKE_BINARY_DIR}/installer"
                "${CMAKE_SOURCE_DIR}/installer/Access808.iss"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/installer"
        COMMENT "Building the Access808 installer"
        VERBATIM)
    add_dependencies(installer Access808_VST3)
endif()
