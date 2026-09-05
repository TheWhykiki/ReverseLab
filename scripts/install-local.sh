#!/bin/bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
# Usage remains install-local.sh [Configuration] (Release by default).
# The Python transaction never signs or merges bundles. --test-root is an
# explicitly isolated command-test seam, not a general destination override.
exec python3 - "$project_dir" "$@" <<'PY'
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import re
import signal
import stat
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def reject_symlink_ancestors(path):
    # Do not silently follow a redirected destination or source/test input.
    for component in (path, *path.parents):
        require(not component.is_symlink(), "Symlink path is not an install target: " + str(component))


def bundle_fingerprint(bundle):
    require(bundle.is_dir() and not bundle.is_symlink(), "Bundle is missing or is a symlink: " + str(bundle))
    result = {}
    for directory, names, files in os.walk(bundle, followlinks=False):
        for path in (Path(directory), *(Path(directory) / name for name in sorted(names + files))):
            relative = str(path.relative_to(bundle))
            info = path.lstat()
            mode = stat.S_IMODE(info.st_mode)
            if stat.S_ISLNK(info.st_mode):
                link = os.readlink(path)
                require(not os.path.isabs(link) and path.resolve(strict=True).is_relative_to(bundle.resolve()),
                        "Bundle symlink escapes its bundle: " + str(path))
                result[relative] = ("link", link)
            elif stat.S_ISDIR(info.st_mode):
                result[relative] = ("directory", mode)
            else:
                require(stat.S_ISREG(info.st_mode), "Unsupported bundle entry: " + str(path))
                digest = hashlib.sha256()
                with path.open("rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(block)
                result[relative] = ("file", mode, info.st_size, digest.hexdigest())
    return result


def validate_bundle(bundle):
    require(bundle.name == "ReverseLab.vst3", "Unexpected bundle name")
    fingerprint = bundle_fingerprint(bundle)
    binary = bundle / "Contents/MacOS/ReverseLab"
    require(binary.is_file() and not binary.is_symlink() and binary.stat().st_size > 0
            and os.access(binary, os.X_OK), "Missing executable ReverseLab bundle binary")
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    require(info.get("CFBundleIdentifier") == "audio.whykiki.reverselab"
            and info.get("CFBundleExecutable") == "ReverseLab", "Wrong ReverseLab bundle identity")
    return fingerprint


def file_identity(path):
    info = path.lstat()
    return info.st_dev, info.st_ino


def install(project, arguments):
    parser = argparse.ArgumentParser(description="Verify and transactionally install the local ReverseLab VST3")
    parser.add_argument("configuration", nargs="?", default="Release")
    parser.add_argument("--test-root", type=Path, help=argparse.SUPPRESS)
    options = parser.parse_args(arguments)
    require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", options.configuration) is not None,
            "Configuration must be a single non-traversing build name")

    if options.test_root is not None:
        root = options.test_root
        require(root.is_absolute() and not root.is_symlink() and root.is_dir(), "Invalid isolated test root")
        root = root.resolve(strict=True)
        require(root.parent == Path("/tmp").resolve() and root.name.startswith("reverselab-install-test-")
                and root.stat().st_uid == os.getuid(), "Test root must be an owned /tmp fixture")
        marker = root / ".reverselab-installer-test"
        require(marker.is_file() and not marker.is_symlink()
                and marker.read_text() == "isolated-reverselab-installer-v1\n", "Missing isolated installer test marker")
        source = root / "source/ReverseLab.vst3"
        destination_dir = root / "destination"
        codesign, ditto = root / "bin/codesign", root / "bin/ditto"
        for tool in (codesign, ditto):
            reject_symlink_ancestors(tool)
            require(tool.is_file() and os.access(tool, os.X_OK), "Missing isolated test command: " + str(tool))
    else:
        require(sys.platform == "darwin", "Local VST3 installation requires macOS")
        source = project / "build/ReverseLab_artefacts" / options.configuration / "VST3/ReverseLab.vst3"
        destination_dir = Path.home() / "Library/Audio/Plug-Ins/VST3"
        codesign, ditto = Path("/usr/bin/codesign"), Path("/usr/bin/ditto")

    reject_symlink_ancestors(source)
    reject_symlink_ancestors(destination_dir)
    destination = destination_dir / "ReverseLab.vst3"
    require(not destination.is_symlink(), "Existing destination is a symlink; refusing to follow it")
    require(not destination.exists() or destination.is_dir(), "Existing destination is not a bundle directory")
    require(source.resolve() != destination.resolve(), "Source and destination must be distinct")
    expected = validate_bundle(source)

    def verify(bundle):
        subprocess.run([str(codesign), "--verify", "--deep", "--strict", "--verbose=2", str(bundle)], check=True)

    verify(source)  # Read-only: an invalid source is never repaired/resigned here.
    require(bundle_fingerprint(source) == expected, "Source changed during signature verification")
    destination_dir.mkdir(parents=True, exist_ok=True)
    lock = destination_dir / ".ReverseLab.install.lock"
    # mkdir is the cross-process exclusion primitive. Never remove another run's
    # lock, including a stale one: report it for explicit inspection instead.
    lock.mkdir()
    stage = backup_dir = backup_bundle = candidate_identity = None
    try:
        stage = Path(tempfile.mkdtemp(prefix=".ReverseLab-install-", dir=destination_dir))
        staged_bundle = stage / "ReverseLab.vst3"
        subprocess.run([str(ditto), str(source), str(staged_bundle)], check=True)
        require(validate_bundle(staged_bundle) == expected, "Staged bundle differs from verified source")
        verify(staged_bundle)
        require(bundle_fingerprint(staged_bundle) == expected, "Staged bundle changed during signature verification")
        require(bundle_fingerprint(source) == expected, "Source changed during staging; refusing mixed provenance")
        candidate_identity = file_identity(staged_bundle)
        require(not destination.is_symlink() and (not destination.exists() or destination.is_dir()),
                "Destination changed to an unexpected file type during staging")
        if destination.exists():
            backup_dir = Path(tempfile.mkdtemp(prefix=".ReverseLab-backup-", dir=destination_dir))
            backup_bundle = backup_dir / "ReverseLab.vst3"
            os.rename(destination, backup_bundle)
        # Both names are on the destination filesystem. The new directory is
        # fresh, so old-only files can never survive by directory merging.
        require(not destination.exists(), "Destination appeared during installation")
        os.rename(staged_bundle, destination)
        require(validate_bundle(destination) == expected, "Installed bundle differs from verified source")
        verify(destination)
        require(bundle_fingerprint(destination) == expected, "Installed bundle changed during final verification")
        require(bundle_fingerprint(source) == expected, "Source changed before installation completed")
    except BaseException:
        # SIGINT/SIGTERM and normal failures all take this path. Match the exact
        # directory inode we staged, so an unrelated concurrent replacement is
        # never deleted/overwritten while trying to roll back our candidate.
        try:
            if candidate_identity is not None and destination.exists() and file_identity(destination) == candidate_identity:
                os.rename(destination, stage / "rejected-install.vst3")
            if backup_bundle is not None and backup_bundle.exists():
                require(not os.path.lexists(destination), "Rollback blocked by a new destination; backup retained")
                os.rename(backup_bundle, destination)
                backup_dir.rmdir()
                print("Restored previous installation: " + str(destination), file=sys.stderr)
            if stage is not None:
                print("Failed staging evidence retained: " + str(stage), file=sys.stderr)
        except BaseException as rollback_error:
            print("ROLLBACK FAILED: " + str(rollback_error) + "; backup: " + str(backup_bundle)
                  + "; staging: " + str(stage), file=sys.stderr)
        raise
    else:
        stage.rmdir()
        print("Installed " + str(destination))
        if backup_bundle is not None:
            print("Backup retained: " + str(backup_bundle))
    finally:
        lock.rmdir()


def interrupted(signum, _frame):
    raise InterruptedError("Installation interrupted by signal " + str(signum))


signal.signal(signal.SIGTERM, interrupted)
try:
    install(Path(sys.argv[1]).resolve(), sys.argv[2:])
except (Exception, KeyboardInterrupt) as error:
    print("Installation failed: " + str(error), file=sys.stderr)
    sys.exit(1)
PY
