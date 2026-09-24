#!/usr/bin/env python3
"""Apply a reviewable v21 resource/smali patch to a disposable build snapshot.

The original project is only read. Apktool's pinned decoder and assembler remain
the code-generation tools; this helper guards inputs, applies exact replacements,
and compares every primary-DEX class after a fresh disassembly of the final APK.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath
from zipfile import ZIP_STORED, ZipFile


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def child(root: Path, relative: str) -> Path:
    part = PurePosixPath(relative)
    if not relative or "\\" in relative or ":" in relative or part.is_absolute() or ".." in part.parts:
        raise ValueError(f"Unsafe relative patch path: {relative!r}")
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError(f"Patch path escapes its root: {relative}")
    return result


def verify_baseline(project: Path, baseline: dict) -> dict:
    if baseline.get("schemaVersion") != 1 or baseline.get("versionCode") != 21:
        raise ValueError("Patch pipeline requires the immutable v21 baseline manifest")
    expected = {entry["path"]: entry for entry in baseline["files"]}
    if len(expected) != baseline["fileCount"]:
        raise ValueError("Duplicate or inconsistent entries in baseline manifest")
    actual = {
        path.relative_to(project).as_posix(): path
        for path in project.rglob("*")
        if path.is_file() and path.relative_to(project).parts[0] != "build"
    }
    if expected.keys() != actual.keys():
        missing = sorted(expected.keys() - actual.keys())[:10]
        extra = sorted(actual.keys() - expected.keys())[:10]
        raise ValueError(f"Project baseline file-set mismatch: missing={missing}, extra={extra}")
    for relative, entry in expected.items():
        data = child(project, relative).read_bytes()
        if len(data) != entry["bytes"] or digest(data) != entry["sha256"]:
            raise ValueError(f"Project does not match the immutable v21 baseline: {relative}")
    return {"fileCount": len(expected), "versionCode": 21, "allHashesMatch": True}


def load_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path, dict, dict]:
    module = args.module_root.resolve()
    snapshot = args.snapshot.resolve()
    work = args.work_directory.resolve()
    source = module / "project"
    if snapshot == source or snapshot.is_relative_to(source) or source.is_relative_to(snapshot):
        raise ValueError("Patch snapshot must be separate from the canonical project")
    if snapshot.parent != work.parent or work == snapshot or work.is_relative_to(source):
        raise ValueError("Patch work directory and snapshot must be separate build-output siblings")
    patch = read_json(args.patch_directory.resolve() / "patch.json")
    if patch.get("schemaVersion") != 1 or patch.get("baselineFile") != "baseline/project-files.json":
        raise ValueError("Patch schemaVersion must be 1 and baselineFile must be baseline/project-files.json")
    if not isinstance(patch.get("targetVersionCode"), int) or patch["targetVersionCode"] <= 21:
        raise ValueError("Patch targetVersionCode must be an integer greater than 21")
    if not re.fullmatch(r"[A-Za-z0-9._+-]+", patch.get("targetVersionName", "")):
        raise ValueError("Invalid patch targetVersionName")
    baseline = read_json(module / patch["baselineFile"])
    return module, snapshot, work, patch, baseline


def decode_primary(raw: Path, output: Path, args: argparse.Namespace) -> Path:
    archive = output.with_suffix(".zip")
    with ZipFile(archive, "x", compression=ZIP_STORED) as package:
        package.write(raw, "classes.dex")
    command = [str(args.java), "-Xmx2g", "-Dfile.encoding=UTF-8", "-Duser.language=en",
               "-Duser.country=US", "-jar", str(args.apktool), "d", str(archive),
               "--no-res", "--no-assets", "--jobs", "2", "-o", str(output)]
    result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", check=False)
    output.with_suffix(".log").write_text(result.stdout + "\n" + result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"Primary DEX decode failed ({result.returncode}): {output.with_suffix('.log')}")
    smali = output / "smali"
    if not smali.is_dir() or not any(smali.rglob("*.smali")):
        raise ValueError("Primary DEX decoder produced no smali files")
    return smali


def exact_edit(data: bytes, edit: dict) -> bytes:
    before_hash = edit.get("beforeSha256")
    if before_hash is not None and digest(data) != before_hash:
        raise ValueError(f"beforeSha256 mismatch: {edit['path']}")
    # Work on bytes: untouched UTF-8 data and CRLF/LF are preserved verbatim.
    data.decode("utf-8")
    for replacement in edit["replacements"]:
        old = replacement["old"].encode("utf-8")
        new = replacement["new"].encode("utf-8")
        count = replacement.get("count", 1)
        if not old or not isinstance(count, int) or count < 1 or old == new:
            raise ValueError(f"Invalid replacement: {edit['path']}")
        if data.count(old) != count:
            raise ValueError(f"Expected {count} exact replacement match(es), found {data.count(old)}: {edit['path']}")
        data = data.replace(old, new)
    after_hash = edit.get("afterSha256")
    if after_hash is not None and digest(data) != after_hash:
        raise ValueError(f"afterSha256 mismatch: {edit['path']}")
    return data


def prepare(args: argparse.Namespace) -> None:
    module, snapshot, work, patch, baseline = load_inputs(args)
    if work.exists():
        raise ValueError(f"Refusing to reuse patch work directory: {work}")
    source_check = verify_baseline(module / "project", baseline)
    snapshot_check = verify_baseline(snapshot, baseline)
    work.mkdir()
    smali_edits = patch.get("smaliEdits", [])
    report = {
        "schemaVersion": 1, "status": "PREPARED", "patchSha256": digest((args.patch_directory / "patch.json").read_bytes()),
        "baselineManifestSha256": digest((module / patch["baselineFile"]).read_bytes()),
        "sourceBaselineBefore": source_check, "snapshotBaselineBefore": snapshot_check,
        "targetVersionCode": patch["targetVersionCode"], "targetVersionName": patch["targetVersionName"],
        "primaryDexRebuilt": bool(smali_edits), "edits": [],
    }
    if smali_edits:
        raw = work / "baseline-primary.dex"
        shutil.copyfile(snapshot / "classes.dex", raw)
        report["baselinePrimaryDexSha256"] = digest(raw.read_bytes())
        decoded = decode_primary(raw, work / "baseline-decoded", args)
        shutil.copytree(decoded, snapshot / "smali")
    seen: set[str] = set()
    for kind in ("resourceEdits", "smaliEdits"):
        for edit in patch.get(kind, []):
            relative = edit["path"]
            permitted = (relative.startswith("smali/") and relative.endswith(".smali")) if kind == "smaliEdits" else (
                relative == "AndroidManifest.xml" or relative.startswith("res/") and relative.endswith(".xml"))
            if not permitted or relative in seen:
                raise ValueError(f"Duplicate or unsupported {kind} path: {relative}")
            seen.add(relative)
            path = child(snapshot, relative)
            original = path.read_bytes()
            changed = exact_edit(original, edit)
            path.write_bytes(changed)
            report["edits"].append({"path": relative, "kind": kind, "beforeSha256": digest(original), "afterSha256": digest(changed)})
    yml = snapshot / "apktool.yml"
    original = yml.read_bytes()
    changed = exact_edit(original, {"path": "apktool.yml", "replacements": [
        {"old": "  versionCode: 21", "new": f"  versionCode: {patch['targetVersionCode']}"},
        {"old": "  versionName: 2.3.6-mvtbot.2", "new": f"  versionName: {patch['targetVersionName']}"},
    ]})
    yml.write_bytes(changed)
    report["edits"].append({"path": "apktool.yml", "kind": "generatedVersion", "beforeSha256": digest(original), "afterSha256": digest(changed)})
    if smali_edits:
        # Only the disposable snapshot loses its raw primary DEX. Its complete
        # original bytes are retained above; secondary DEX files stay raw.
        (snapshot / "classes.dex").unlink()
        all_smali = sorted((snapshot / "smali").rglob("*.smali"))
        report["expectedSmaliFiles"] = {path.relative_to(snapshot / "smali").as_posix(): digest(path.read_bytes()) for path in all_smali}
    write_json(work / "ui-patch-report.json", report)
    print(f"Patch prepared: {len(report['edits'])} files; primary DEX rebuilt={bool(smali_edits)}")


def normalize_smali(text: str) -> str:
    """Ignore only whitespace and equivalent DEX encoding/label choices.

    baksmali labels contain code offsets; width changes to strings/jumps can
    shift them. Canonicalize labels by occurrence without changing strings,
    comments, register operands, method bodies, annotations or debug metadata.
    Explicit type-appropriate static defaults may be elided by the assembler.
    """
    labels: dict[str, str] = {}
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith(".method "):
            labels = {}
        if line.startswith(".field "):
            line = normalize_static_default(line)
        if not line.startswith(("#", ".source ")):
            # Smali field descriptors contain ':L...'; only replace true labels
            # at a token boundary, not field names or types.
            line = re.sub(r"^const-string/jumbo\s", "const-string ", line)
            line = re.sub(r"^goto/(?:16|32)\s", "goto ", line)
            pieces = re.split(r'("(?:\\.|[^"\\])*")', line)
            for index in range(0, len(pieces), 2):
                pieces[index] = re.sub(r"(?<![A-Za-z0-9_>$;]):[A-Za-z_][A-Za-z_0-9]*", lambda m: substitute_label(m.group(0), labels), pieces[index])
            line = "".join(pieces)
        lines.append(line)
    return "\n".join(lines)


def normalize_static_default(line: str) -> str:
    # DEX class_def_item.static_values_off: omitted trailing static values have
    # their type-appropriate zero/null value. Only forms observed in this pinned
    # input are accepted; no floating-point, instance-field or nonzero values.
    # https://source.android.com/docs/core/runtime/dex-format#class-def-item
    declaration, separator, value = line.rpartition(" = ")
    if not separator or "static" not in declaration.split()[:-1]:
        return line
    field_type = declaration.rpartition(":")[2]
    if (field_type, value) in {("Z", "false"), ("I", "0x0"), ("J", "0x0L")}:
        return declaration
    if value == "null" and re.fullmatch(r"(?:L[^;\s]+;|\[+(?:[ZBSCIJFD]|L[^;\s]+;))", field_type):
        return declaration
    return line


def substitute_label(label: str, labels: dict[str, str]) -> str:
    if label not in labels:
        labels[label] = f":label_{len(labels)}"
    return labels[label]


def verify(args: argparse.Namespace) -> None:
    module, snapshot, work, patch, baseline = load_inputs(args)
    report_path = work / "ui-patch-report.json"
    report = read_json(report_path)
    if report["patchSha256"] != digest((args.patch_directory / "patch.json").read_bytes()):
        raise ValueError("Patch definition changed during this build")
    if report["baselineManifestSha256"] != digest((module / patch["baselineFile"]).read_bytes()):
        raise ValueError("Baseline manifest changed during this build")
    report["sourceBaselineAfter"] = verify_baseline(module / "project", baseline)
    for edit in report["edits"]:
        if digest(child(snapshot, edit["path"]).read_bytes()) != edit["afterSha256"]:
            raise ValueError(f"Expected patched snapshot changed during build: {edit['path']}")
    if report["primaryDexRebuilt"]:
        raw = work / "rebuilt-primary.dex"
        with ZipFile(args.apk) as apk:
            if apk.namelist().count("classes.dex") != 1:
                raise ValueError("Final APK must have exactly one primary DEX")
            raw.write_bytes(apk.read("classes.dex"))
        report["rebuiltPrimaryDexSha256"] = digest(raw.read_bytes())
        if report["rebuiltPrimaryDexSha256"] == report["baselinePrimaryDexSha256"]:
            raise ValueError("Primary DEX was not rebuilt despite a smali patch")
        decoded = decode_primary(raw, work / "rebuilt-decoded", args)
        actual_files = {path.relative_to(decoded).as_posix(): path for path in decoded.rglob("*.smali")}
        expected_files = report["expectedSmaliFiles"]
        if actual_files.keys() != expected_files.keys():
            raise ValueError("Rebuilt DEX class set differs from the expected primary DEX")
        exact_count = 0
        normalized_count = 0
        failures = []
        for relative, expected_hash in expected_files.items():
            expected = child(snapshot / "smali", relative).read_bytes()
            if digest(expected) != expected_hash:
                raise ValueError(f"Snapshot smali changed during build: {relative}")
            actual = actual_files[relative].read_bytes()
            if digest(actual) == expected_hash:
                exact_count += 1
            elif normalize_smali(actual.decode("utf-8")) == normalize_smali(expected.decode("utf-8")):
                normalized_count += 1
            else:
                failures.append(relative)
        if failures:
            report["roundtripFailures"] = failures
            report["status"] = "FAILED"
            write_json(report_path, report)
            raise ValueError(f"Rebuilt DEX differs from expected patched smali in {len(failures)} classes: {failures[:10]}")
        edited_classes = sum(1 for edit in report["edits"] if edit["kind"] == "smaliEdits")
        report["dexRoundtrip"] = {"classCount": len(expected_files), "exactClassCount": exact_count,
                                  "normalizedClassCount": normalized_count, "patchedClassCount": edited_classes,
                                  "unpatchedClassCount": len(expected_files) - edited_classes,
                                  "allClassesMatchExpected": True}
    # Keep the detailed per-class expected hashes as a separate local artifact.
    if "expectedSmaliFiles" in report:
        write_json(work / "expected-smali-hashes.json", report.pop("expectedSmaliFiles"))
    report["status"] = "PASS"
    write_json(report_path, report)
    print("Patch verification: PASS; source baseline unchanged; rebuilt DEX matches expected patched smali")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "verify"))
    parser.add_argument("--module-root", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--patch-directory", type=Path, required=True)
    parser.add_argument("--work-directory", type=Path, required=True)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--apktool", type=Path, required=True)
    parser.add_argument("--apk", type=Path)
    args = parser.parse_args()
    if args.action == "verify" and args.apk is None:
        parser.error("verify requires --apk")
    try:
        (prepare if args.action == "prepare" else verify)(args)
    except (ValueError, OSError, KeyError) as exc:
        print(f"Patch pipeline failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
