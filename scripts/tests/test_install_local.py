"""Installer transaction regressions: all writes are confined to /tmp fixtures."""

import ast
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest import mock


INSTALLER = Path(__file__).resolve().parents[1] / "install-local.sh"

FAKE_CODESIGN = r'''#!/usr/bin/env python3
import json, pathlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
args = sys.argv[1:]
with (root / "codesign.log").open("a") as log:
    log.write(json.dumps(args) + "\n")
if args[:-1] != ["--verify", "--deep", "--strict", "--verbose=2"]:
    sys.exit(90)
bundle = pathlib.Path(args[-1])
mode = (root / "mode").read_text() if (root / "mode").exists() else ""
if mode == "reject-source" and bundle == root / "source/ReverseLab.vst3": sys.exit(11)
if mode == "reject-stage" and ".ReverseLab-install-" in str(bundle): sys.exit(12)
if mode == "reject-target" and bundle == root / "destination/ReverseLab.vst3": sys.exit(13)
if mode == "tamper-target" and bundle == root / "destination/ReverseLab.vst3":
    (bundle / "Contents/Resources/recipe.txt").write_text("changed after copy")
if mode == "signal-target" and bundle == root / "destination/ReverseLab.vst3":
    import os, signal
    os.kill(os.getppid(), signal.SIGTERM)
'''

FAKE_DITTO = r'''#!/usr/bin/env python3
import pathlib, shutil, sys
root = pathlib.Path(__file__).resolve().parents[1]
source, target = map(pathlib.Path, sys.argv[1:])
mode = (root / "mode").read_text() if (root / "mode").exists() else ""
if target.exists(): sys.exit(91)
if mode == "fail-copy":
    target.mkdir()
    (target / "partial.txt").write_text("incomplete")
    sys.exit(14)
shutil.copytree(source, target, symlinks=True)
if mode == "tamper-copy": (target / "Contents/Resources/recipe.txt").write_text("tampered")
if mode == "source-changed": (source / "Contents/Resources/recipe.txt").write_text("concurrent source change")
'''


def tree(path):
    if not path.exists():
        return None
    result = {}
    for entry in [path, *sorted(path.rglob("*"))]:
        name = str(entry.relative_to(path))
        if entry.is_symlink():
            result[name] = ("link", os.readlink(entry))
        elif entry.is_dir():
            result[name] = ("directory", entry.stat().st_mode & 0o777)
        else:
            result[name] = ("file", entry.stat().st_mode & 0o777, entry.read_bytes())
    return result


class LocalInstallerTests(unittest.TestCase):
    def setUp(self):
        # Never invoke the old script with fake HOME/PATH: it would still be able
        # to install into the real user's VST3 directory. Missing test mode is RED.
        self.assertIn("--test-root", INSTALLER.read_text(), "installer lacks safe isolated test inputs")
        self.temp = tempfile.TemporaryDirectory(prefix="reverselab-install-test-", dir="/tmp")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / ".reverselab-installer-test").write_text("isolated-reverselab-installer-v1\n")
        (self.root / "bin").mkdir()
        for name, content in {"codesign": FAKE_CODESIGN, "ditto": FAKE_DITTO}.items():
            tool = self.root / "bin" / name
            tool.write_text(content)
            tool.chmod(0o755)
        self.source = self.root / "source/ReverseLab.vst3"
        (self.source / "Contents/MacOS").mkdir(parents=True)
        (self.source / "Contents/Resources").mkdir()
        (self.source / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": "audio.whykiki.reverselab", "CFBundleExecutable": "ReverseLab"}))
        binary = self.source / "Contents/MacOS/ReverseLab"
        binary.write_bytes(b"dummy executable; never launched\n")
        binary.chmod(0o755)
        (self.source / "Contents/Resources/recipe.txt").write_text("new resource\n")
        self.destination = self.root / "destination/ReverseLab.vst3"
        self.source_before = tree(self.source)

    def old_install(self):
        self.destination.mkdir(parents=True)
        (self.destination / "old-only.txt").write_text("must not merge\n")
        (self.destination / "preserve.bin").write_bytes(b"old exact bytes\0\1")
        return tree(self.destination)

    def run_installer(self, mode="", extra=()):
        (self.root / "mode").write_text(mode)
        return subprocess.run(["/bin/bash", str(INSTALLER), "--test-root", str(self.root), *extra],
                              text=True, capture_output=True, timeout=20)

    def assert_no_transaction_lock(self):
        self.assertFalse((self.root / "destination/.ReverseLab.install.lock").exists())

    def assert_verified_only(self):
        calls = [json.loads(line) for line in (self.root / "codesign.log").read_text().splitlines()]
        self.assertTrue(calls)
        for args in calls:
            self.assertEqual(args[:-1], ["--verify", "--deep", "--strict", "--verbose=2"])
            self.assertTrue(Path(args[-1]).is_relative_to(self.root))
        return calls

    def test_clean_replace_retains_exact_backup_and_source(self):
        old = self.old_install()
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(tree(self.source), self.source_before)
        self.assertEqual(tree(self.destination), self.source_before)
        backups = list((self.root / "destination").glob(".ReverseLab-backup-*/ReverseLab.vst3"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(tree(backups[0]), old)
        self.assertIn(str(backups[0]), result.stdout)
        self.assertEqual(len(self.assert_verified_only()), 3)
        self.assert_no_transaction_lock()

    def test_first_install_needs_no_backup(self):
        result = self.run_installer(extra=("Debug",))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(tree(self.destination), self.source_before)
        self.assertFalse(list((self.root / "destination").glob(".ReverseLab-backup-*")))
        self.assert_no_transaction_lock()

    def test_source_stage_copy_and_final_failures_preserve_previous_target(self):
        old = self.old_install()
        for mode in ("reject-source", "reject-stage", "fail-copy", "tamper-copy", "reject-target", "tamper-target", "signal-target"):
            with self.subTest(mode=mode):
                result = self.run_installer(mode)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("Installed ", result.stdout)
                self.assertEqual(tree(self.destination), old)
                self.assertEqual(tree(self.source), self.source_before)
                self.assert_no_transaction_lock()
        self.assert_verified_only()

    def test_failed_first_install_does_not_leave_a_target(self):
        result = self.run_installer("reject-target")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())
        self.assertEqual(tree(self.source), self.source_before)
        self.assert_no_transaction_lock()

    def test_source_change_during_copy_is_not_published(self):
        old = self.old_install()
        result = self.run_installer("source-changed")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(self.destination), old)
        self.assert_no_transaction_lock()

    def test_wrong_bundle_identity_rejected_before_copy(self):
        old = self.old_install()
        info = self.source / "Contents/Info.plist"
        info.write_bytes(plistlib.dumps({"CFBundleIdentifier": "another.plugin", "CFBundleExecutable": "ReverseLab"}))
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(self.destination), old)
        self.assertFalse((self.root / "codesign.log").exists())

    def test_config_path_traversal_rejected(self):
        old = self.old_install()
        for config in ("../Release", "/tmp", "..", "Release/../../.."):
            with self.subTest(configuration=config):
                result = self.run_installer(extra=(config,))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(tree(self.destination), old)
        self.assertFalse((self.root / "codesign.log").exists())

    def test_destination_symlink_is_not_followed(self):
        outside = self.root / "sentinel"
        outside.mkdir()
        (outside / "keep.txt").write_text("untouched")
        before = tree(outside)
        self.destination.parent.symlink_to(outside, target_is_directory=True)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(outside), before)

    def test_external_bundle_symlink_is_rejected(self):
        old = self.old_install()
        (self.source / "Contents/Resources/escape").symlink_to(self.root / "bin", target_is_directory=True)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(self.destination), old)

    def test_another_installer_lock_is_never_removed(self):
        old = self.old_install()
        lock = self.root / "destination/.ReverseLab.install.lock"
        lock.mkdir()
        (lock / "owner.txt").write_text("other installer")
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(self.destination), old)
        self.assertEqual((lock / "owner.txt").read_text(), "other installer")

    def test_test_root_requires_owned_marker(self):
        old = self.old_install()
        (self.root / ".reverselab-installer-test").unlink()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tree(self.destination), old)
        self.assertFalse((self.root / "codesign.log").exists())

    def transaction_namespace(self):
        # Exercise rename failures in the exact embedded installer functions;
        # no source rewrite, shell-default invocation or HOME/PATH override.
        code = INSTALLER.read_text().split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        parsed = ast.parse(code)
        definitions = [node for node in parsed.body if isinstance(node, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
        namespace = {}
        exec(compile(ast.Module(body=definitions, type_ignores=[]), str(INSTALLER), "exec"), namespace)
        return namespace

    def test_publish_failure_restores_target_after_backup_rename(self):
        old = self.old_install()
        namespace = self.transaction_namespace()
        actual_rename = os.rename

        def fail_publish(source, destination):
            if Path(destination) == self.destination and Path(source).parent.name.startswith(".ReverseLab-install-"):
                raise OSError("injected candidate publish failure")
            return actual_rename(source, destination)

        with mock.patch("os.rename", side_effect=fail_publish):
            with self.assertRaisesRegex(OSError, "injected candidate publish failure"):
                namespace["install"](INSTALLER.parents[1], ["--test-root", str(self.root)])
        self.assertEqual(tree(self.destination), old)
        self.assertEqual(tree(self.source), self.source_before)
        self.assert_no_transaction_lock()

    def test_rollback_failure_retains_exact_backup_and_original_error(self):
        old = self.old_install()
        namespace = self.transaction_namespace()
        (self.root / "mode").write_text("reject-target")
        actual_rename = os.rename

        def fail_restore(source, destination):
            if Path(source).parent.name.startswith(".ReverseLab-backup-"):
                raise OSError("injected rollback failure")
            return actual_rename(source, destination)

        with mock.patch("os.rename", side_effect=fail_restore):
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                namespace["install"](INSTALLER.parents[1], ["--test-root", str(self.root)])
        self.assertEqual(raised.exception.returncode, 13)
        backups = list((self.root / "destination").glob(".ReverseLab-backup-*/ReverseLab.vst3"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(tree(backups[0]), old)
        self.assertFalse(self.destination.exists())
        self.assertEqual(tree(self.source), self.source_before)
        self.assert_no_transaction_lock()


if __name__ == "__main__":
    unittest.main()
