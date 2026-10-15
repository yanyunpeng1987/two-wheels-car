"""Publish verified immutable firmware packages. No build or hardware access.

Catalog paths are relative to this repository or an explicitly named sibling.
Existing release files must match byte-for-byte; otherwise bump packageRevision.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs/releases/firmware-catalog.json"
SHA = lambda data: hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def immutable(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"Immutable file differs; use a new revision: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as handle:
        handle.write(data)


def hex_layout(data):
    base = 0
    memory = {}
    eof = False
    for line in data.decode("ascii").splitlines():
        if not line.startswith(":") or eof:
            raise ValueError("Invalid HEX line or data after EOF")
        b = bytes.fromhex(line[1:])
        if len(b) < 5 or len(b) != b[0] + 5 or sum(b) % 256:
            raise ValueError("Invalid HEX length/checksum")
        address = int.from_bytes(b[1:3], "big")
        kind = b[3]
        if kind == 0:
            for index, value in enumerate(b[4:-1]):
                absolute = base + address + index
                if not 0x08000000 <= absolute < 0x08020000:
                    raise ValueError("HEX outside STM32F401RB 128 KiB Flash")
                if 0x08004000 <= absolute < 0x08008000:
                    raise ValueError("HEX enters protected PID Sector1")
                if absolute in memory:
                    raise ValueError("Overlapping HEX data")
                memory[absolute] = value
        elif kind in (2, 4):
            if b[0] != 2 or address != 0:
                raise ValueError("Invalid extended address record")
            base = int.from_bytes(b[4:6], "big") << (4 if kind == 2 else 16)
        elif kind == 1:
            if b[0] != 0 or address != 0:
                raise ValueError("Invalid EOF")
            eof = True
        elif kind in (3, 5):
            if b[0] != 4 or address != 0:
                raise ValueError("Invalid start address record")
        else:
            raise ValueError("Unsupported HEX record")
    if not eof or not memory:
        raise ValueError("Missing HEX data/EOF")
    ranges = []
    for address in sorted(memory):
        if not ranges or address != ranges[-1][1] + 1:
            ranges.append([address, address])
        else:
            ranges[-1][1] = address
    return {"dataBytes": len(memory), "rangesInclusive": [
        [f"0x{start:08X}", f"0x{end:08X}"] for start, end in ranges],
        "pidSectorHasData": False}


def load_release(item, catalog):
    datetime.date.fromisoformat(item["buildDate"])
    if not re.fullmatch(r"r[1-9][0-9]*", item["revision"]):
        raise ValueError("Firmware revisions must be rN")
    if not isinstance(item["packageRevision"], int) or item["packageRevision"] < 1:
        raise ValueError("Invalid package revision")
    project = item["sourceProject"]
    if project != "." and not re.fullmatch(r"[A-Za-z0-9_-]+", project):
        raise ValueError("Expected repository or named sibling")
    project_root = ROOT if project == "." else ROOT.parent / project
    source = (project_root / item["sourceDirectory"]).resolve()
    if not source.is_relative_to(project_root.resolve()):
        raise ValueError("Source directory escaped project")
    original = (source / "manifest.json").read_bytes()
    manifest = json.loads(original)
    stem = item["originalStem"]
    if not re.fullmatch(r"[A-Za-z0-9_-]+", stem):
        raise ValueError("Invalid source filename")
    data = {}
    originals = {}
    for extension in ("hex", "axf", "map"):
        name = stem + "." + extension
        blob = (source / name).read_bytes()
        expected = manifest["artifacts"][name]
        if SHA(blob) != expected["sha256"] or len(blob) != expected["bytes"]:
            raise ValueError(f"Artifact differs from source manifest: {name}")
        data[extension] = blob
        originals[extension] = name
    if SHA(data["hex"]) != item["hexSha256"]:
        raise ValueError("HEX differs from pinned release catalog")
    current = item["revision"] == catalog["currentRevision"]
    if current and (not manifest.get("onVehicle", {}).get("userAcceptance") or
                    not manifest["onVehicle"].get("flashReadbackVerified")):
        raise ValueError("Current release must have user acceptance and readback evidence")
    if current:
        for rel, digest in manifest["sourceSha256"].items():
            p = (project_root / rel).resolve()
            if not p.is_relative_to(project_root.resolve()) or SHA(p.read_bytes()) != digest:
                raise ValueError(f"Current source differs from accepted artifact: {rel}")
    layout = hex_layout(data["hex"])
    base = f'{catalog["product"]}_{catalog["target"]}_Gamepad_{item["revision"]}_{item["buildDate"].replace("-", "")}_{item["hexSha256"][:8]}'
    package_id = base + f'_p{item["packageRevision"]}'
    files = {base + ".hex": data["hex"], "debug/" + base + ".axf": data["axf"],
             "debug/" + base + ".map": data["map"]}
    notes = f'''# {item["title"]}

固件版本：{item["revision"]}；构建日期：{item["buildDate"]}；交付包版本：p{item["packageRevision"]}。

烧录文件：`{base}.hex`。这是原验收HEX的逐字节副本，仅规范文件名，未重新编译。

## 本版修改

''' + "\n".join("- " + change for change in item["changes"]) + f'''

## 兼容与验证范围

{item["compatibility"]}

{item["acceptance"]}

{item["limits"]}

## 烧录与校验

HEX SHA-256：`{item["hexSha256"]}`。

目标为本工程的STM32F401RB/128KiB和匹配的硬件、电机映射。先备份当前板完整Flash与PID，仅擦除Sector 0/2/3/4，保留Sector 1（0x08004000～0x08007FFF）。烧录后校验程序及PID原始字节。不要全片擦除；HEX不含PID数据不等于烧录工具会自动保护PID。

`debug/`的AXF/MAP供调试和地址追溯，不是另一套待选固件。未附带其他设备的参数、整区BIN或诊断固件。

文件校验见SHA256SUMS.txt和manifest.json；SHA用于内容一致性检查，不是数字签名。固件版本号是本次交付标识，不声称已写入设备屏幕显示或Git标签。
'''
    files["RELEASE_NOTES.md"] = notes.encode("utf-8")
    info = {"schemaVersion": 1, "packageId": package_id,
            "firmwareRevision": item["revision"], "packageRevision": item["packageRevision"],
            "buildDate": item["buildDate"], "packageDate": catalog["packageDate"],
            "product": catalog["product"], "mcu": catalog["target"],
            "status": item["status"], "title": item["title"],
            "entryHex": base + ".hex", "hexSha256": item["hexSha256"],
            "changes": item["changes"], "compatibility": item["compatibility"],
            "acceptance": item["acceptance"], "limits": item["limits"],
            "layout": layout, "eraseSectors": [0, 2, 3, 4],
            "preserveRangeInclusive": ["0x08004000", "0x08007FFF"],
            "provenance": {"sourceManifestSha256": SHA(original), "originalFiles": originals,
                           "sourceSha256": manifest.get("sourceSha256", {}),
                           "currentSourceVerified": current,
                           "uncommittedSource": manifest.get("sourceUncommitted", manifest.get("uncommittedChanges", True)),
                           "recompiled": False},
            "files": {name: {"sha256": SHA(blob), "bytes": len(blob)} for name, blob in sorted(files.items())}}
    files["manifest.json"] = json_bytes(info)
    files["SHA256SUMS.txt"] = ("\n".join(f"{SHA(blob)}  {name}" for name, blob in sorted(files.items())) + "\n").encode("ascii")
    return item, package_id, info, files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT.parent / "BalanceCar-Deliveries")
    args = parser.parse_args()
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    for key in ("product", "target"):
        if not re.fullmatch(r"[A-Za-z0-9_-]+", catalog[key]):
            raise ValueError("Product/target names must be ASCII filename components")
    datetime.date.fromisoformat(catalog["packageDate"])
    prepared = [load_release(item, catalog) for item in catalog["releases"]]
    if len({i[0]["revision"] for i in prepared}) != len(prepared):
        raise ValueError("Duplicate firmware revision")
    selected = [i for i in prepared if i[0]["revision"] == catalog["currentRevision"]]
    if len(selected) != 1:
        raise ValueError("Require exactly one current release")
    out = args.output.resolve()
    if out == ROOT.resolve() or out == ROOT.parent.resolve():
        raise ValueError("Use a dedicated delivery directory")
    history = catalog["packageHistory"]
    recorded = {}
    package_history = set()
    for event in history:
        datetime.date.fromisoformat(event["date"])
        for firmware in event["firmware"]:
            revision, digest = firmware["revision"], firmware["hexSha256"]
            if revision in recorded and recorded[revision] != digest:
                raise ValueError("Firmware bytes changed without a new rN")
            recorded[revision] = digest
            package_history.add((revision, event["packageRevision"]))
    for item, _, _, _ in prepared:
        if recorded.get(item["revision"]) != item["hexSha256"] or (item["revision"], item["packageRevision"]) not in package_history:
            raise ValueError("Record every firmware/package revision in packageHistory")
    previous_index = out / "CATALOG.json"
    if previous_index.exists():
        previous = json.loads(previous_index.read_text(encoding="utf-8"))
        old_history = previous.get("packageHistory", [])
        if history[:len(old_history)] != old_history:
            raise ValueError("Package history is append-only")
        previous_digests = {r["revision"]: r["hexSha256"] for r in previous["releases"]}
        for item, _, _, _ in prepared:
            if item["revision"] in previous_digests and previous_digests[item["revision"]] != item["hexSha256"]:
                raise ValueError("Refuse to reuse a firmware revision for different bytes")
    entries = []
    for item, package_id, info, files in prepared:
        folder = out / "releases" / package_id
        existing = {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()} if folder.exists() else set()
        if existing - files.keys():
            raise ValueError(f"Unexpected files in immutable package: {folder}")
        for name, blob in files.items():
            immutable(folder / name, blob)
            if SHA((folder / name).read_bytes()) != SHA(blob):
                raise ValueError("Copy verification failed")
        entry = {"revision": item["revision"], "packageId": package_id, "status": item["status"],
                 "hex": (folder / info["entryHex"]).relative_to(out).as_posix(),
                 "package": folder.relative_to(out).as_posix(), "hexSha256": info["hexSha256"]}
        entries.append(entry)
    current = next(e for e in entries if e["revision"] == catalog["currentRevision"])
    # A deterministic archive: the same version/package revision has one hash.
    import io
    archive = io.BytesIO()
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as z:
        for name, blob in sorted(selected[0][3].items()):
            zi = ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            zi.compress_type = ZIP_DEFLATED
            z.writestr(zi, blob)
    zip_name = current["packageId"] + ".zip"
    immutable(out / zip_name, archive.getvalue())
    current.update(zip=zip_name, zipSha256=SHA(archive.getvalue()))
    (out / "CURRENT.json").write_bytes(json_bytes(current))
    (out / "CATALOG.json").write_bytes(json_bytes({"schemaVersion": 1, "currentRevision": catalog["currentRevision"], "releases": entries, "packageHistory": history}))
    for src, dst in [("tools/Verify-Release.ps1", "Verify-Release.ps1")]:
        (out / dst).write_bytes((ROOT / src).read_bytes())
    entry = (f'# 当前可用固件：{current["revision"]}\n\n'
             f'[直接使用的HEX]({current["hex"]}) · [完整交付ZIP]({current["zip"]}) · '
             '[版本修改记录](VERSIONS.md)\n\n'
             f'HEX SHA-256：`{current["hexSha256"]}`。HEX/AXF/MAP均与原验收产物逐字节一致。\n\n---\n\n')
    (out / "README.md").write_text(entry + (ROOT / "docs/FIRMWARE_DELIVERY.md").read_text(encoding="utf-8"), encoding="utf-8")
    history_text = "# 固件版本修改记录\n\n当前推荐：**" + catalog["currentRevision"] + "**。版本不连续是因为中间实验版不作为交付。\n\n"
    history_text += "| 版本 | 日期 | 定位 | 验收及兼容范围 |\n|---|---|---|---|\n"
    for item, package_id, info, files in prepared:
        history_text += f'| [{item["revision"]}](releases/{package_id}/RELEASE_NOTES.md) | {item["buildDate"]} | {item["title"]} | {item["acceptance"]} |\n'
    history_text += "\n## 打包记录（只追加）\n\n"
    for event in history:
        revisions = ", ".join(r["revision"] for r in event["firmware"])
        history_text += f'- {event["date"]} / p{event["packageRevision"]} / {revisions}：{event["changes"]}\n'
    (out / "VERSIONS.md").write_text(history_text, encoding="utf-8")
    (out / "ZIP_SHA256SUMS.txt").write_text(f'{current["zipSha256"]}  {zip_name}\n', encoding="ascii")
    print(json.dumps({"output": str(out), "current": current, "releaseCount": len(entries)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
