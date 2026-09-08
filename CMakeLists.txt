cmake_minimum_required(VERSION 3.25)
if(CMAKE_HOST_APPLE)
    # These cache initialisers must precede project(), because CMake uses them while
    # selecting and checking the Apple compiler toolchain. They are Apple-only; Visual
    # Studio selects each Windows architecture independently with -A.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal macOS build" FORCE)
endif()
project(ReverseLab VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # The standalone VST3/MSI must also run on clean Windows installations that
    # do not already have a matching Visual C++ Redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(external/JUCE)

if(WIN32)
    # JUCE 8.0.15 calls these x86_64 and arm64ec internally and places the
    # binaries in the matching VST3 bundle folders. ARM64EC is intentional for
    # Windows-on-Arm hosts such as Cubase; a plain ARM64 build uses a different
    # bundle ABI/folder and is not the Windows-on-Arm product target we validate.
    set(REVERSELAB_SUPPORTED_WINDOWS_ARCHITECTURES x86_64 arm64ec)
    if(NOT JUCE_TARGET_ARCHITECTURE IN_LIST REVERSELAB_SUPPORTED_WINDOWS_ARCHITECTURES)
        message(FATAL_ERROR
            "Unsupported ReverseLab Windows architecture '${JUCE_TARGET_ARCHITECTURE}'. "
            "Configure Visual Studio with -A x64 or -A ARM64EC.")
    endif()

    # Reject a manually overridden or stale JUCE architecture cache instead of
    # putting one PE architecture into another architecture's VST3 folder.
    if(CMAKE_GENERATOR MATCHES "^Visual Studio ")
        if(JUCE_TARGET_ARCHITECTURE STREQUAL "x86_64")
            set(REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM x64)
        else()
            set(REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM ARM64EC)
        endif()
        if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM)
            message(FATAL_ERROR
                "Visual Studio platform '${CMAKE_GENERATOR_PLATFORM}' does not match JUCE's "
                "detected architecture '${JUCE_TARGET_ARCHITECTURE}'. Use a fresh build directory "
                "with -A ${REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM}.")
        endif()
    endif()

    # ARM64EC is a supported product only when its target helpers and host test
    # can execute on the build machine. This rejects an x64-to-ARM64EC
    # cross-build instead of presenting compile-only evidence as validation.
    if(JUCE_TARGET_ARCHITECTURE STREQUAL "arm64ec")
        if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio ")
            message(FATAL_ERROR "ReverseLab ARM64EC builds require a Visual Studio generator.")
        endif()
        string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" REVERSELAB_WINDOWS_HOST_ARCHITECTURE)
        if(NOT REVERSELAB_WINDOWS_HOST_ARCHITECTURE MATCHES "^(arm64|arm64ec|aarch64)$"
           OR NOT JUCE_WINDOWS_HELPERS_CAN_RUN)
            message(FATAL_ERROR
                "ReverseLab ARM64EC builds require native Windows on Arm so the VST3 manifest "
                "helper and host test can run. Configure this target on an Arm64 runner.")
        endif()
    endif()
endif()

# JUCE 8.0.15 omits Visual Studio's -A value from its nested VST3 manifest-helper
# configure. The pinned, repo-local override forwards x64 or ARM64EC without
# modifying the JUCE submodule.
include(cmake/JuceVST3ManifestHelperPlatform.cmake)

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

if(WIN32)
    set(REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY "" CACHE STRING
        "P-256 release-gate public key as 128 uppercase hex characters (X then Y)")
    set(REVERSELAB_WINDOWS_RELEASE_GATE_NEXT_PUBLIC_KEY_XY "" CACHE STRING
        "Optional next P-256 release-gate public key as 128 uppercase hex characters (X then Y)")
endif()

include(cmake/Updater.cmake)
wk_add_updater(ReverseLab)

if(WIN32)
    # These well-known points (private scalars 1 and 2) are used only by the
    # non-distribution self-test/compile-shape binaries. Production must always
    # receive separately managed public keys through the cache entries above.
    set(reverselab_test_release_gate_public_key_xy
        "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C2964FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5")
    set(reverselab_test_release_gate_next_public_key_xy
        "7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC4766997807775510DB8ED040293D9AC69F7430DBBA7DADE63CE982299E04B79D227873D1")
    foreach(updater_test_target IN ITEMS
            ReverseLabWindowsUpdaterSelfTests
            ReverseLabWindowsUpdaterProductionShape)
        if(TARGET ${updater_test_target})
            target_compile_definitions(${updater_test_target} PRIVATE
                WK_WINDOWS_UPDATER_RELEASE_GATE_PUBLIC_KEY_XY="${reverselab_test_release_gate_public_key_xy}"
                WK_WINDOWS_UPDATER_RELEASE_GATE_NEXT_PUBLIC_KEY_XY="${reverselab_test_release_gate_next_public_key_xy}")
        endif()
    endforeach()

    if(TARGET ReverseLabWindowsUpdater)
        string(LENGTH "${REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY}"
            reverselab_release_gate_public_key_length)
        if(NOT reverselab_release_gate_public_key_length EQUAL 128 OR
           NOT REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY MATCHES "^[0-9A-F]+$")
            message(FATAL_ERROR
                "REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY must be exactly 128 uppercase hexadecimal characters (P-256 X then Y)")
        endif()
        set(reverselab_release_gate_next_public_key_xy
            "${REVERSELAB_WINDOWS_RELEASE_GATE_NEXT_PUBLIC_KEY_XY}")
        if(NOT reverselab_release_gate_next_public_key_xy STREQUAL "")
            string(LENGTH "${reverselab_release_gate_next_public_key_xy}"
                reverselab_release_gate_next_public_key_length)
            if(NOT reverselab_release_gate_next_public_key_length EQUAL 128 OR
               NOT reverselab_release_gate_next_public_key_xy MATCHES "^[0-9A-F]+$")
                message(FATAL_ERROR
                    "REVERSELAB_WINDOWS_RELEASE_GATE_NEXT_PUBLIC_KEY_XY must be empty or exactly 128 uppercase hexadecimal characters (P-256 X then Y)")
            endif()
            if(reverselab_release_gate_next_public_key_xy STREQUAL
               REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY)
                message(FATAL_ERROR
                    "REVERSELAB_WINDOWS_RELEASE_GATE_NEXT_PUBLIC_KEY_XY must differ from REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY")
            endif()
        endif()
        target_compile_definitions(ReverseLabWindowsUpdater PRIVATE
            WK_WINDOWS_UPDATER_RELEASE_GATE_PUBLIC_KEY_XY="${REVERSELAB_WINDOWS_RELEASE_GATE_PUBLIC_KEY_XY}"
            WK_WINDOWS_UPDATER_RELEASE_GATE_NEXT_PUBLIC_KEY_XY="${reverselab_release_gate_next_public_key_xy}")
    endif()
endif()

if(APPLE)
    # The updater command above embeds the helper first. Sign nested code inside-out, then sign
    # the completed VST3 root so moduleinfo.json and the helper are covered by its resource seal.
    find_program(REVERSELAB_CODESIGN_EXECUTABLE codesign REQUIRED)
    set(REVERSELAB_CODESIGN_IDENTITY "-" CACHE STRING
        "Final VST3/updater signing identity; '-' selects ad-hoc signing")
    set(REVERSELAB_CODESIGN_OPTIONS --force --sign "${REVERSELAB_CODESIGN_IDENTITY}")
    if(NOT REVERSELAB_CODESIGN_IDENTITY STREQUAL "-")
        list(APPEND REVERSELAB_CODESIGN_OPTIONS --options runtime --timestamp)
    endif()
    add_custom_command(TARGET ReverseLab_VST3 POST_BUILD
        COMMAND "${REVERSELAB_CODESIGN_EXECUTABLE}" ${REVERSELAB_CODESIGN_OPTIONS}
            "$<TARGET_BUNDLE_DIR:ReverseLab_VST3>/Contents/Helpers/ReverseLabUpdater.app"
        COMMAND "${REVERSELAB_CODESIGN_EXECUTABLE}" ${REVERSELAB_CODESIGN_OPTIONS}
            "$<TARGET_BUNDLE_DIR:ReverseLab_VST3>"
        COMMAND "${REVERSELAB_CODESIGN_EXECUTABLE}" --verify --deep --strict --verbose=2 "$<TARGET_BUNDLE_DIR:ReverseLab_VST3>"
        COMMENT "Signing ReverseLab updater and completed VST3 bundle inside-out"
        VERBATIM)
endif()

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
set_tests_properties(ReverseLabDSP PROPERTIES TIMEOUT 300)
# Keep the real held-listener/control-order regression independently bounded.
add_test(NAME ReverseLabListenerLock COMMAND ReverseLabTests --listener-lock-only)
set_tests_properties(ReverseLabListenerLock PROPERTIES TIMEOUT 20)

juce_add_console_app(ReverseLabParameterTextTests PRODUCT_NAME "ReverseLab Parameter Text Tests")
target_sources(ReverseLabParameterTextTests PRIVATE Tests/ParameterTextTests.cpp)
target_include_directories(ReverseLabParameterTextTests PRIVATE Source)
target_compile_definitions(ReverseLabParameterTextTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ReverseLabParameterTextTests PRIVATE juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME ReverseLabParameterTextRoundtrip COMMAND ReverseLabParameterTextTests)
set_tests_properties(ReverseLabParameterTextRoundtrip PROPERTIES TIMEOUT 300)

if(APPLE OR WIN32)
    juce_add_console_app(ReverseLabHostTests PRODUCT_NAME "ReverseLab VST3 Host Tests")
    target_sources(ReverseLabHostTests PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(ReverseLabHostTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(ReverseLabHostTests PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(ReverseLabHostTests ReverseLab_VST3)
    add_test(NAME ReverseLabBundleLoad COMMAND ReverseLabHostTests
        "$<GENEX_EVAL:$<TARGET_PROPERTY:ReverseLab_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set_tests_properties(ReverseLabBundleLoad PROPERTIES TIMEOUT 300)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ReverseLabBuildPortability COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_build_portability.py")
add_test(NAME ReverseLabWindowsUpdaterContract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_updater.py")
add_test(NAME ReverseLabWindowsInstallerContract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_installer_contract.py")
add_test(NAME ReverseLabWindowsIntegrationContract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_integration.py")
add_test(NAME ReverseLabWindowsReleaseContract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_release_contract.py")
add_test(NAME ReverseLabWindowsReleaseBootstrap COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_release_bootstrap.py")
add_test(NAME ReverseLabWindowsReleaseState COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_windows_release_state.py")
# These harnesses exercise macOS release/install scripts with POSIX permissions,
# signals, symlinks and /bin/bash. Their fake-tool tests are useful on macOS and
# Linux, but registering them on Windows would be a platform error rather than a
# product regression.
if(NOT WIN32)
    add_test(NAME ReverseLabReleasePipeline COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_release_pipeline.py")
    add_test(NAME ReverseLabLocalInstaller COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests" -p "test_install_local.py")
endif()
add_test(NAME ReverseLabAcceptanceValidators COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v test_analyse_host_audio)
set_tests_properties(ReverseLabAcceptanceValidators PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/acceptance")

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

# Preset persistence and complete factory-bank audio checks.
juce_add_console_app(ReverseLabPresetTests PRODUCT_NAME "ReverseLab Preset Tests")
target_sources(ReverseLabPresetTests PRIVATE Tests/PresetTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ReverseEngine.cpp)
target_include_directories(ReverseLabPresetTests PRIVATE Source)
target_compile_definitions(ReverseLabPresetTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1 PRESET_TEST_SUBLAB=0 JucePlugin_Name="ReverseLab"
    REVERSELAB_RELEASE_CHANNEL="Stable" REVERSELAB_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ReverseLabPresetTests PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME ReverseLabPresets COMMAND ReverseLabPresetTests)
set_tests_properties(ReverseLabPresets PROPERTIES TIMEOUT 600)

if(APPLE)
    # Test-only Cocoa observation proves that native file panels really open and
    # close. Product code and non-macOS tests do not depend on this bridge.
    # Updater.cmake enables Objective-C++; sanitizer configurations must pass
    # matching CMAKE_OBJCXX_FLAGS so this lifecycle bridge is instrumented too.
    target_sources(ReverseLabPresetTests PRIVATE Tests/NativeFilePanel.mm)
    # A fresh process per transition gives each real panel a tight,
    # independently reported failure. The sequential test below separately
    # proves that no AppKit modal state leaks from one case into the next.
    foreach(native_operation IN ITEMS import export)
        foreach(native_action IN ITEMS ancestor-hide detach destroy hide-then-destroy)
            set(native_case "${native_operation}-${native_action}")
            string(REPLACE "-" "_" native_case_id "${native_case}")
            set(native_test "ReverseLabNativeChooser_${native_case_id}")
            add_test(NAME "${native_test}" COMMAND "${CMAKE_COMMAND}" -E env
                "WHYKIKI_PRESET_TEST_NATIVE_ONLY=1"
                "WHYKIKI_PRESET_TEST_NATIVE_CASE=${native_case}"
                "$<TARGET_FILE:ReverseLabPresetTests>")
            set_tests_properties("${native_test}" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    # Also run every transition in one process. This catches a native modal
    # session that appears closed but poisons the next chooser; isolated cases
    # alone cannot detect that lifecycle leak.
    add_test(NAME ReverseLabNativeChoosersSequential COMMAND "${CMAKE_COMMAND}" -E env
        --unset=WHYKIKI_PRESET_TEST_NATIVE_CASE
        "WHYKIKI_PRESET_TEST_NATIVE_ONLY=1"
        "$<TARGET_FILE:ReverseLabPresetTests>")
    set_tests_properties(ReverseLabNativeChoosersSequential PROPERTIES TIMEOUT 480 RUN_SERIAL TRUE)
endif()
