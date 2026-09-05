#!/usr/bin/env python3
"""Static contracts for the supported macOS and Windows build matrix."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class BuildPortabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.helper_override = (
            ROOT / "cmake" / "JuceVST3ManifestHelperPlatform.cmake"
        ).read_text(encoding="utf-8")
        cls.juce_cmake = (ROOT / "external" / "JUCE" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.juce_utils = (
            ROOT / "external" / "JUCE" / "extras" / "Build" / "CMake" / "JUCEUtils.cmake"
        ).read_text(encoding="utf-8")
        cls.juce_arch_detection = (
            ROOT
            / "external"
            / "JUCE"
            / "extras"
            / "Build"
            / "CMake"
            / "juce_runtime_arch_detection.cpp"
        ).read_text(encoding="utf-8")
        cls.vst3_windows_loader = (
            ROOT
            / "external"
            / "JUCE"
            / "modules"
            / "juce_audio_processors_headless"
            / "format_types"
            / "VST3_SDK"
            / "public.sdk"
            / "source"
            / "vst"
            / "hosting"
            / "module_win32.cpp"
        ).read_text(encoding="utf-8")
        cls.ci = (ROOT / ".github" / "workflows" / "ci.yml").read_text(
            encoding="utf-8"
        )

    def test_macos_toolchain_settings_precede_project(self) -> None:
        project = self.cmake.index("project(ReverseLab")
        deployment = self.cmake.index("CMAKE_OSX_DEPLOYMENT_TARGET")
        architectures = self.cmake.index("CMAKE_OSX_ARCHITECTURES")
        self.assertLess(deployment, project)
        self.assertLess(architectures, project)
        self.assertTrue(self.cmake.startswith("cmake_minimum_required(VERSION 3.25)\nif(CMAKE_HOST_APPLE)"))
        self.assertRegex(
            self.cmake,
            r'CMAKE_OSX_ARCHITECTURES\s+"arm64;x86_64"',
        )

    def test_windows_targets_are_x64_and_native_arm64ec(self) -> None:
        self.assertIn(
            "set(REVERSELAB_SUPPORTED_WINDOWS_ARCHITECTURES x86_64 arm64ec)",
            self.cmake,
        )
        self.assertIn(
            'JUCE_TARGET_ARCHITECTURE STREQUAL "arm64ec"',
            self.cmake,
        )
        self.assertIn('CMAKE_GENERATOR MATCHES "^Visual Studio "', self.cmake)
        self.assertIn("CMAKE_HOST_SYSTEM_PROCESSOR", self.cmake)
        self.assertIn("OR NOT JUCE_WINDOWS_HELPERS_CAN_RUN", self.cmake)
        self.assertIn("REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM x64", self.cmake)
        self.assertIn("REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM ARM64EC", self.cmake)
        self.assertIn(
            "CMAKE_GENERATOR_PLATFORM STREQUAL REVERSELAB_EXPECTED_VISUAL_STUDIO_PLATFORM",
            self.cmake,
        )
        self.assertIn("require native Windows on Arm", self.cmake)
        self.assertNotIn("VST3_AUTO_MANIFEST FALSE", self.cmake)
        self.assertNotIn("REVERSELAB_VST3_AUTO_MANIFEST", self.cmake)
        self.assertIn("if(APPLE OR WIN32)", self.cmake)
        self.assertIn("add_test(NAME ReverseLabBundleLoad", self.cmake)
        self.assertIn("JUCE_PLUGIN_ARTEFACT_FILE", self.cmake)

    def test_juce_maps_windows_on_arm_to_arm64ec_vst3_layout(self) -> None:
        self.assertRegex(
            self.juce_arch_detection,
            r"(?s)defined\(_M_ARM64EC\).*?JUCE_ARCH arm64ec",
        )
        self.assertRegex(
            self.juce_utils,
            r'(?s)JUCE_TARGET_ARCHITECTURE STREQUAL "arm64ec"\).*?set\(windows_arch "arm64ec"\)',
        )
        for directory in ('"arm64ec-win"', '"arm64x-win"', '"x86_64-win"'):
            self.assertIn(directory, self.vst3_windows_loader)

    def test_reviewed_juce_helper_override_forwards_visual_studio_platform(self) -> None:
        version = re.search(r"project\(JUCE VERSION ([0-9.]+)", self.juce_cmake)
        self.assertIsNotNone(version)
        self.assertEqual(version.group(1), "8.0.15")

        upstream = re.search(
            r"function\(_juce_add_vst3_manifest_helper_target\b.*?\nendfunction\(\)",
            self.juce_utils,
            re.DOTALL,
        )
        self.assertIsNotNone(upstream)
        self.assertNotIn("CMAKE_GENERATOR_PLATFORM", upstream.group(0))

        override = re.search(
            r"function\(_juce_add_vst3_manifest_helper_target\b.*?\nendfunction\(\)",
            self.helper_override,
            re.DOTALL,
        )
        self.assertIsNotNone(override)
        reviewed_delta = (
            '    list(APPEND PASSTHROUGH_ARGS "-A${CMAKE_GENERATOR_PLATFORM}")\n\n'
        )
        self.assertEqual(override.group(0).count(reviewed_delta), 1)
        self.assertEqual(override.group(0).replace(reviewed_delta, ""), upstream.group(0))

        self.assertIn('STREQUAL "8.0.15"', self.helper_override)
        self.assertIn(
            "function(_juce_add_vst3_manifest_helper_target", self.helper_override
        )
        self.assertIn(
            'list(APPEND PASSTHROUGH_ARGS "-A${CMAKE_GENERATOR_PLATFORM}")',
            self.helper_override,
        )
        self.assertIn('CMAKE_GENERATOR MATCHES "^Visual Studio "', self.helper_override)
        self.assertIn('CMAKE_GENERATOR_PLATFORM STREQUAL "x64"', self.helper_override)
        self.assertIn('CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64EC"', self.helper_override)

        juce = self.cmake.index("add_subdirectory(external/JUCE)")
        override = self.cmake.index(
            "include(cmake/JuceVST3ManifestHelperPlatform.cmake)"
        )
        plugin = self.cmake.index("juce_add_plugin(ReverseLab")
        self.assertLess(juce, override)
        self.assertLess(override, plugin)

    def test_windows_ci_is_native_isolated_and_runtime_validated(self) -> None:
        windows = self.ci.split("\n  windows:\n", 1)[1].split(
            "\n  linux-tests:\n", 1
        )[0]

        for expected in (
            "runner: windows-2022",
            "runner_arch: X64",
            "processor_arch: AMD64",
            "generator: Visual Studio 17 2022",
            "platform: x64",
            "build_dir: build-windows-x86_64",
            "bundle_arch: x86_64-win",
            "runner: windows-11-vs2026-arm",
            "runner_arch: ARM64",
            "processor_arch: ARM64",
            "generator: Visual Studio 18 2026",
            "platform: ARM64EC",
            "build_dir: build-windows-arm64ec",
            "bundle_arch: arm64ec-win",
            "Verify native runner architecture",
            "Contents/Resources/moduleinfo.json",
            "Unexpected VST3 architecture directories",
            "8664 machine \\(x64\\)",
            "ARM64X",
            'WHYKIKI_PRESET_TEST_SAVE_RESTORE_ONLY: "1"',
            '-E "^ReverseLabPresets$"',
            '-R "^ReverseLabPresets$"',
            "ReverseLabHostTests.exe",
        ):
            self.assertIn(expected, windows)

        self.assertNotIn("CMAKE_GENERATOR_PLATFORM:", windows)
        self.assertEqual(windows.count("dumpbin_host:"), 2)
        self.assertEqual(windows.count("dumpbin_target:"), 2)
        self.assertIn("bin/${{ matrix.dumpbin_host }}/${{ matrix.dumpbin_target }}", windows)

    def test_macos_ci_runs_universal_acceptance_on_both_native_slices(self) -> None:
        macos = self.ci.split("\n  macos:\n", 1)[1].split("\n  windows:\n", 1)[0]
        for expected in (
            "runner: macos-15",
            "runner_arch: ARM64",
            "native_arch: arm64",
            "build_dir: build-macos-arm64",
            "runner: macos-15-intel",
            "runner_arch: X64",
            "native_arch: x86_64",
            "build_dir: build-macos-x86_64",
            "Verify native runner architecture",
            'test "$(uname -m)"',
            'test "$architectures" = "arm64 x86_64"',
            'for arch in arm64 x86_64',
            "vtool -arch",
            "ReverseLabHostTests",
            "ReverseLab-macOS-universal-${{ matrix.artifact_arch }}",
        ):
            self.assertIn(expected, macos)

        self.assertNotIn("runs-on: macos-14", self.ci)
        self.assertEqual(self.ci.count("runs-on: macos-15"), 1)


if __name__ == "__main__":
    unittest.main()
