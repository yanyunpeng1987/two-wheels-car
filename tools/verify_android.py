#!/usr/bin/env python3
"""Read-only MVTBOT repository checks, with optional local import verification.

The default works in a clean clone without proprietary APK-derived inputs.
--project-root checks an existing local project against the immutable v21 import;
later intentional edits fail that check and must never be overwritten to pass it.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "Android" / "MVTBOT"
LOCAL_DIRECTORIES = {"project", "materials", "build", "validation"}
PRIVATE_SUFFIXES = {".p12", ".pfx", ".jks", ".keystore", ".dpapi", ".pem", ".key"}
REQUIRED_FILES = (
    "README.md", "AGENTS.md", ".gitignore", "docs/HANDOFF.md",
    "docs/PROTOCOL_COORDINATION.md", "docs/CONSOLIDATION.md",
    "changes/README.md", "branding/app-icon.png", "tools/Build-MVTBOT.ps1",
    "tools/Verify-Import.py", "tools/Activate-Android.ps1",
    "tools/Check-AndroidEnvironment.ps1", "baseline/project-files.json",
    "tools/prepare_link_build.py", "tools/verify_link_apk.py", "tools/verify_combined_ui.py",
    "tools/smali_equivalence.py",
    "changes/APP-UI-001/patch.json", "changes/APP-UI-001/verify_release.py",
    "changes/LINK-001/patch_smali.py", "changes/LINK-001/smali-baseline.json",
    "changes/LINK-001/ui-import.json",
    "link/src/com/mvtbot/link/FrameDecoder.java",
    "link/src/com/mvtbot/link/ProtocolValidation.java",
    "link/src/com/mvtbot/link/MiniBalanLink.java",
)


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def is_sha256(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value)


def is_private_file(path):
    name = path.name.lower()
    return (path.suffix.lower() in PRIVATE_SUFFIXES or name == ".env"
            or name.startswith(".env.") or ".local." in name)


def read_baseline():
    baseline = json.loads((MODULE / "baseline/project-files.json").read_text(encoding="utf-8"))
    # An explicit schema keeps archive source paths and device metadata out of Git.
    require(isinstance(baseline, dict), "Baseline must be a JSON object")
    require(set(baseline) == {
        "schemaVersion", "packageName", "versionCode", "versionName",
        "releaseApkSha256", "originalBaseApkSha256", "fileCount", "totalBytes", "files",
    }, "Unexpected baseline fields; only version metadata and file hashes are allowed")
    require(type(baseline["schemaVersion"]) is int and baseline["schemaVersion"] == 1,
            "Unsupported baseline schema")
    require(baseline["packageName"] == "com.Wonder.bot"
            and type(baseline["versionCode"]) is int and baseline["versionCode"] == 21
            and baseline["versionName"] == "2.3.6-mvtbot.2", "Unexpected v21 baseline identity")
    for key in ("releaseApkSha256", "originalBaseApkSha256"):
        require(is_sha256(baseline[key]), f"Invalid {key}")
    entries = baseline["files"]
    require(isinstance(entries, list) and entries, "Baseline files must be a nonempty list")
    paths = set()
    total = 0
    for entry in entries:
        require(isinstance(entry, dict) and set(entry) == {"path", "bytes", "sha256"},
                "Each baseline file must contain only path, bytes and sha256")
        value = entry["path"]
        require(isinstance(value, str) and value and not any(ord(c) < 32 for c in value),
                "Invalid baseline path")
        path = PurePosixPath(value)
        require(not path.is_absolute() and "\\" not in value and ":" not in value
                and all(part not in ("", ".", "..") for part in value.split("/"))
                and path.parts[0] not in LOCAL_DIRECTORIES,
                "Baseline paths must be normalized relative paths inside project/")
        require(not is_private_file(path), f"Private material is forbidden in baseline: {value}")
        require(value.casefold() not in paths, f"Duplicate baseline path: {value}")
        require(type(entry["bytes"]) is int and entry["bytes"] >= 0, f"Invalid byte count: {value}")
        require(is_sha256(entry["sha256"]), f"Invalid SHA-256: {value}")
        paths.add(value.casefold())
        total += entry["bytes"]
    require(type(baseline["fileCount"]) is int and baseline["fileCount"] == len(entries),
            "Baseline file count differs")
    require(type(baseline["totalBytes"]) is int and baseline["totalBytes"] == total,
            "Baseline total byte count differs")
    require({"androidmanifest.xml", "apktool.yml", "classes.dex"} <= paths,
            "Baseline is missing the manifest, Apktool configuration or DEX")
    return baseline


def check_repository():
    for relative in REQUIRED_FILES:
        require((MODULE / relative).is_file(), f"Missing Android repository file: {relative}")
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z", "--", "Android/MVTBOT"],
        check=True, capture_output=True,
    )
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        path = PurePosixPath(raw.decode("utf-8"))
        relative = path.relative_to("Android/MVTBOT")
        require(relative.parts[0] not in LOCAL_DIRECTORIES,
                f"Local-only Android material is tracked by Git: {relative}")
        require(not is_private_file(relative) and relative.suffix.lower() not in {".apk", ".apks", ".aab"},
                f"Private material or packaged application is tracked by Git: {relative}")
    probes = [f"Android/MVTBOT/{directory}/__policy_check__" for directory in sorted(LOCAL_DIRECTORIES)]
    probes += [f"Android/MVTBOT/__policy_check__{suffix}" for suffix in sorted(PRIVATE_SUFFIXES)]
    probes += ["Android/MVTBOT/.env", "Android/MVTBOT/settings.local.json"]
    ignored = subprocess.run(
        ["git", "-C", str(ROOT), "check-ignore", "--stdin", "-z"],
        input=("\0".join(probes) + "\0").encode("utf-8"), capture_output=True,
    )
    require(ignored.returncode in (0, 1), "Cannot check Android Git ignore policy")
    require(set(ignored.stdout.decode("utf-8").rstrip("\0").split("\0")) == set(probes),
            "Android ignore policy no longer excludes all local directories and private signing/configuration files")


def check_project(project_root, baseline):
    project_root = project_root.resolve()
    require(project_root.is_dir(), "Local project directory does not exist; restore it from the full project archive")
    expected = {entry["path"] for entry in baseline["files"]}
    failures = []
    for entry in baseline["files"]:
        path = (project_root / entry["path"]).resolve()
        if not path.is_relative_to(project_root) or not path.is_file():
            failures.append(f"Missing file or path outside project: {entry['path']}")
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if path.stat().st_size != entry["bytes"] or digest.hexdigest() != entry["sha256"]:
            failures.append(f"Changed file: {entry['path']}")
    for path in project_root.rglob("*"):
        relative = path.relative_to(project_root)
        # Apktool's project-root cache is generated; nested build metadata is input.
        if relative.parts[0] == "build":
            continue
        if path.is_file() and relative.as_posix() not in expected:
            failures.append(f"Extra file: {relative.as_posix()}")
    require(not failures, "Local project differs from the immutable imported baseline:\n"
            + "\n".join(failures[:20])
            + (f"\n... {len(failures)} differences total" if len(failures) > 20 else "")
            + "\nIntentional edits also fail. Preserve and version the changes; do not overwrite them to pass this check.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path,
                        help="Optional existing local Apktool project/ directory to verify read-only")
    args = parser.parse_args()
    try:
        check_repository()
        baseline = read_baseline()
        print(f"PASS: Android repository policy and baseline ({baseline['fileCount']} files, {baseline['totalBytes']} bytes)")
        if args.project_root:
            check_project(args.project_root, baseline)
            print("PASS: local project files match the v21 import baseline (no files changed)")
        else:
            print("Local project contents not checked; use --project-root with restored materials. This is not an APK build or device test.")
        return 0
    except (VerificationError, OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
