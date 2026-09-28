#!/usr/bin/env python3
"""Build LINK-001 code in an isolated v21 project snapshot; never touch devices."""
import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, log):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(a) for a in args], stdout=stream,
                                stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Tool failed ({result.returncode}); see {log}")


def verify_snapshot(module, snapshot):
    baseline = json.loads((module / "baseline/project-files.json").read_text(encoding="utf-8"))
    expected = {x["path"]: x for x in baseline["files"]}
    actual = {p.relative_to(snapshot).as_posix() for p in snapshot.rglob("*")
              if p.is_file() and p.relative_to(snapshot).parts[0] != "build"}
    if actual != set(expected):
        raise ValueError("Snapshot does not contain exactly the immutable v21 input files")
    for rel, info in expected.items():
        if sha(snapshot / rel) != info["sha256"]:
            raise ValueError(f"Snapshot baseline mismatch: {rel}")
    return baseline


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module-root", type=Path, required=True)
    ap.add_argument("--snapshot", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--java-home", type=Path, required=True)
    ap.add_argument("--android-sdk", type=Path, required=True)
    ap.add_argument("--apktool", type=Path, required=True)
    ap.add_argument("--build-tools-version", default="36.0.0")
    ap.add_argument("--rollback-ui-only", action="store_true")
    ap.add_argument("--version-code", type=int)
    ap.add_argument("--version-name")
    args = ap.parse_args()
    module, snapshot, output = (p.resolve() for p in (args.module_root, args.snapshot, args.output))
    if snapshot == (module / "project").resolve() or not snapshot.is_relative_to(output):
        raise ValueError("Only an output-owned project snapshot may be modified")
    baseline = verify_snapshot(module, snapshot)
    config = snapshot / "apktool.yml"
    text = config.read_text(encoding="utf-8")
    version, name = (40, "2.3.6-mvtbot.12-ui-rollback") if args.rollback_ui_only else (39, "2.3.6-mvtbot.12-hc05d")
    if (args.version_code is None) != (args.version_name is None):
        raise ValueError("Version code and name overrides must be supplied together")
    if args.version_code is not None:
        if args.version_code <= 21 or not re.fullmatch(r"[A-Za-z0-9._+-]{1,120}", args.version_name):
            raise ValueError("Version override must advance the baseline and use a plain version name")
        version, name = args.version_code, args.version_name
    text, count = re.subn(r"(?m)^(  versionCode:) 21$", rf"\g<1> {version}", text)
    if count != 1:
        raise ValueError("Missing exact versionCode 21 baseline")
    text, count = re.subn(r"(?m)^(  versionName:) 2\.3\.6-mvtbot\.2$", rf"\g<1> {name}", text)
    if count != 1:
        raise ValueError("Missing exact v21 versionName baseline")
    config.write_text(text, encoding="utf-8", newline="\n")
    report = {"mode": "ui-rollback" if args.rollback_ui_only else "hc05d-ui",
              "versionCode": version, "versionName": name, "baselineVerified": True,
              "sourceProjectModified": False, "deviceOperations": False}
    if (snapshot / "assets/mvtbot-debug-autoconnect.properties").exists():
        raise ValueError("Startup auto-connect configuration is no longer allowed")
    report["debugAutoConnect"] = {"enabled": False, "removed": True}
    java, javac = (args.java_home / "bin" / n for n in ("java.exe", "javac.exe"))
    android = args.android_sdk / "platforms/android-36/android.jar"
    d8 = args.android_sdk / "build-tools" / args.build_tools_version / "lib/d8.jar"
    if not d8.is_file():
        d8 = d8.with_name("r8.jar")
    apk = module / "materials/deliverables/MVTBOT/icon-update-v21/MVTBOT.apk"
    if sha(apk) != baseline["releaseApkSha256"]:
        raise ValueError("The archived v21 APK differs from the pinned baseline")
    for p in (java, javac, android, d8, args.apktool):
        if not p.is_file():
            raise FileNotFoundError(p)
    logic = output / "logic"
    logic.mkdir()
    decoded = logic / "decoded-v21"
    run([java, "-Xmx2g", "-jar", args.apktool, "d", "-r", "--no-assets", "-j", "2",
         "-o", decoded, apk], logic / "decode.log")
    smali = snapshot / "smali"
    shutil.copytree(decoded / "smali", smali)
    ui_path = module / "changes/APP-UI-001/patch.json"
    ui = json.loads(ui_path.read_text(encoding="utf-8"))
    imported = json.loads((module / "changes/LINK-001/ui-import.json").read_text(encoding="utf-8"))
    pinned = next(r["sha256"] for r in imported["files"] if r["path"].endswith("APP-UI-001/patch.json"))
    if sha(ui_path) != pinned:
        raise ValueError("UI patch differs from the imported, reviewed commit")
    spec = importlib.util.spec_from_file_location("ui_patch", module / "tools/Apply-UiPatch.py")
    ui_helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ui_helper)
    edits = []
    for kind in ("resourceEdits", "smaliEdits"):
        for edit in ui[kind]:
            target = ui_helper.child(snapshot, edit["path"])
            data = ui_helper.exact_edit(target.read_bytes(), edit)
            target.write_bytes(data)
            edits.append({"path": edit["path"], "sha256": sha(target)})
    report["ui"] = {"sourceCommit": imported["sourceCommit"], "patchSha256": sha(ui_path), "edits": edits}
    (snapshot / "classes.dex").unlink()
    if not args.rollback_ui_only:
        patch = module / "changes/LINK-001/patch_smali.py"
        run([sys.executable, patch, "--smali-root", smali, "--ui-patch", ui_path], logic / "patch.log")
        report["bridge"] = json.loads((smali / ".mvtbot-link-001.json").read_text(encoding="utf-8"))
        shutil.move(str(smali / ".mvtbot-link-001.json"), str(logic / "bridge-manifest.json"))
        sources = sorted((module / "link/src").rglob("*.java"))
        if not sources:
            raise ValueError("Missing versioned LINK-001 Java sources")
        classes = logic / "classes"
        classes.mkdir()
        run([javac, "-encoding", "UTF-8", "-source", "8", "-target", "8", "-classpath", android,
             "-d", classes, *sources], logic / "javac.log")
        jar = logic / "link.jar"
        with ZipFile(jar, "w", compression=ZIP_DEFLATED) as z:
            for p in sorted(classes.rglob("*.class")):
                entry = ZipInfo(p.relative_to(classes).as_posix(), (2026, 1, 1, 0, 0, 0))
                entry.compress_type = ZIP_DEFLATED
                z.writestr(entry, p.read_bytes())
        dex = logic / "dex"
        dex.mkdir()
        run([java, "-cp", d8, "com.android.tools.r8.D8", "--min-api", "23", "--lib", android,
             "--output", dex, jar], logic / "d8.log")
        if sorted(p.name for p in dex.glob("*.dex")) != ["classes.dex"]:
            raise ValueError("Expected exactly one auxiliary DEX")
        shutil.copyfile(dex / "classes.dex", snapshot / "classes4.dex")
        report["sources"] = {p.relative_to(module).as_posix(): sha(p) for p in sources}
        report["patchSha256"] = sha(patch)
        report["auxiliaryDexSha256"] = sha(snapshot / "classes4.dex")
        report["preservedDex"] = {p.name: sha(p) for p in snapshot.glob("classes[23].dex")}
        report["tools"] = {"androidJarSha256": sha(android), "d8Sha256": sha(d8),
                           "apktoolSha256": sha(args.apktool)}
    else:
        report["preservedDex"] = {p.name: sha(p) for p in snapshot.glob("classes[23].dex")}
    expected_primary = {p.relative_to(smali).as_posix(): sha(p)
                        for p in sorted(smali.rglob("*.smali"))}
    expected_file = logic / "expected-primary-smali.json"
    expected_file.write_text(json.dumps(expected_primary, indent=2), encoding="utf-8")
    report["expectedPrimarySmaliSha256"] = sha(expected_file)
    report["expectedPrimaryClassCount"] = len(expected_primary)
    # ART profiles refer to old method indexes/checksums; regenerate separately if needed.
    removed = []
    for rel in ("assets/dexopt/baseline.prof", "assets/dexopt/baseline.profm"):
        p = snapshot / rel
        if p.is_file():
            p.unlink()
            removed.append(rel)
    report["removedStaleProfiles"] = removed
    (output / "link-build.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Prepared {report['mode']} version {version}; canonical v21 project unchanged")


if __name__ == "__main__":
    main()
