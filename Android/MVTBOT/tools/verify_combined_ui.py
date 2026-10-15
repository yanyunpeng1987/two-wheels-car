#!/usr/bin/env python3
"""Verify both UI and LINK code in a fresh decode of the final, signed APK."""
import argparse
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-root", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--apktool", type=Path, required=True)
    args = parser.parse_args()
    module, snapshot, output = args.module_root.resolve(), args.snapshot.resolve(), args.output.resolve()
    prepared = json.loads((output / "link-build.json").read_text(encoding="utf-8"))
    ui = load("combined_ui_checks", module / "changes/APP-UI-001/verify_release.py")
    helper = load("combined_smali_checks", module / "tools/Apply-UiPatch.py")
    equivalence = load("combined_smali_equivalence", module / "tools/smali_equivalence.py")
    decoded = output / "final-decoded"
    if decoded.exists():
        raise ValueError("Refusing to reuse a previous APK decode")
    command = [str(args.java_home / "bin/java.exe"), "-Xmx2g", "-Dfile.encoding=UTF-8",
               "-jar", str(args.apktool), "d", "--no-assets", "--jobs", "2",
               "--frame-path", str(output / "framework"), "-o", str(decoded), str(args.apk)]
    with (output / "final-decode.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError("Final APK decode failed; see final-decode.log")
    baseline = module / "project"
    before, after = ui.parse(baseline / "AndroidManifest.xml"), ui.parse(decoded / "AndroidManifest.xml")
    ui.require(before.get("package") == after.get("package") == "com.Wonder.bot", "Package changed")
    ui.require(ui.manifest_components(before) == ui.manifest_components(after), "Manifest components changed")
    for key in ("versionCode", "versionName"):
        value = after.get(ui.ANDROID + key)
        if value is not None:
            ui.require(value == str(prepared[key]), f"Wrong final {key}")
    checks = {"publicResourceIds": ui.check_resource_ids(baseline, decoded),
              "layouts": ui.check_layouts(baseline, decoded),
              "strings": ui.check_strings(decoded), "compiledUiRoutes": ui.check_smali(decoded),
              "negativeFixtures": ui.self_test()}
    manifest = output / "logic/expected-primary-smali.json"
    ui.require(digest(manifest) == prepared["expectedPrimarySmaliSha256"], "Expected class manifest changed")
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    actual = {p.relative_to(decoded / "smali").as_posix(): p
              for p in (decoded / "smali").rglob("*.smali")}
    ui.require(set(expected) == set(actual), "Primary class set changed")
    exact = normalized = 0
    failures = []
    for relative, expected_hash in expected.items():
        source = snapshot / "smali" / relative
        ui.require(digest(source) == expected_hash, f"Post-patch source changed: {relative}")
        if digest(actual[relative]) == expected_hash:
            exact += 1
        elif equivalence.equivalent_smali(source.read_text(encoding="utf-8"),
                                         actual[relative].read_text(encoding="utf-8"),
                                         helper.normalize_smali):
            normalized += 1
        else:
            failures.append(relative)
    ui.require(not failures, f"Compiled methods differ from the composed source: {failures[:12]}")
    # The canonical, full v21 input must remain byte-for-byte intact.
    baseline_meta = json.loads((module / "baseline/project-files.json").read_text(encoding="utf-8"))
    helper.verify_baseline(baseline, baseline_meta)
    report = {"status": "PASS", "mode": prepared["mode"], "versionCode": prepared["versionCode"],
              "uiSourceCommit": prepared["ui"]["sourceCommit"], "checks": checks,
              "primaryClassCount": len(expected), "exactClasses": exact, "equivalentClasses": normalized,
              "equivalenceRules": ["method declaration order only; exact signature set",
                                   "original pinned encoding/static-default normalization",
                                   "line-label ordering at the same address",
                                   "single unreachable packed-switch alignment nop"],
              "sourceBaselineUnchanged": True, "deviceOperations": False, "androidRuntimeVerified": False}
    (output / "combined-ui-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2),
                                                         encoding="utf-8")
    print(f"PASS: merged UI routes/resources and all {len(expected)} primary DEX classes verified")


if __name__ == "__main__":
    main()
