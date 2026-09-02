cmake_minimum_required(VERSION 3.25)
project(ReverseLab VERSION 1.0.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    # The shipped product is a universal macOS VST3 with a macOS 11 floor. Other platforms are
    # only used to compile and run the DSP/processor test suite (e.g. in CI), so keep these
    # Apple-only instead of forcing them unconditionally, which breaks configuration elsewhere.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal macOS build" FORCE)
endif()

add_subdirectory(external/JUCE)

juce_add_plugin(ReverseLab
    VERSION ${PROJECT_VERSION}
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
    JUCE_VST3_CAN_REPLACE_VST2=0
    REVERSELAB_RELEASE_CHANNEL="Stable"
    REVERSELAB_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(ReverseLab PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

juce_add_console_app(ReverseLabTests PRODUCT_NAME "ReverseLab Tests")
target_sources(ReverseLabTests PRIVATE
    Tests/Tests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReverseEngine.cpp)
target_include_directories(ReverseLabTests PRIVATE Source)
target_compile_definitions(ReverseLabTests PRIVATE
    REVERSELAB_UNIT_TESTS=1
    JucePlugin_Name="ReverseLab"
    REVERSELAB_RELEASE_CHANNEL="Stable"
    REVERSELAB_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ReverseLabTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME ReverseLabDSP COMMAND ReverseLabTests)

juce_add_console_app(ReverseLabRenderAB PRODUCT_NAME "ReverseLab A-B Renderer")
target_sources(ReverseLabRenderAB PRIVATE
    Tests/RenderAB.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReverseEngine.cpp)
target_include_directories(ReverseLabRenderAB PRIVATE Source)
target_compile_definitions(ReverseLabRenderAB PRIVATE
    JucePlugin_Name="ReverseLab"
    REVERSELAB_RELEASE_CHANNEL="Stable"
    REVERSELAB_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ReverseLabRenderAB PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)
