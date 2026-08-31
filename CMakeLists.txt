cmake_minimum_required(VERSION 3.25)
project(ReverseLab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal macOS build" FORCE)

add_subdirectory(external/JUCE)

juce_add_plugin(ReverseLab
    COMPANY_NAME "Whykiki Audio"
    BUNDLE_ID "audio.whykiki.reverselab"
    PLUGIN_MANUFACTURER_CODE Wkik
    PLUGIN_CODE RvLb
    FORMATS VST3
    PRODUCT_NAME "ReverseLab"
    VST3_CATEGORIES Fx Delay
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ReverseLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ReverseEngine.cpp
    Source/ReverseEngine.h
    Source/Parameters.h)

target_compile_definitions(ReverseLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ReverseLab PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

juce_add_console_app(ReverseLabTests PRODUCT_NAME "ReverseLab Tests")
target_sources(ReverseLabTests PRIVATE
    Tests/Tests.cpp
    Source/ReverseEngine.cpp
    Source/ReverseEngine.h)
target_include_directories(ReverseLabTests PRIVATE Source)
target_link_libraries(ReverseLabTests PRIVATE
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_events
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME ReverseLabDSP COMMAND ReverseLabTests)
