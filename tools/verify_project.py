#!/usr/bin/env python3
"""Read-only checks for the BalanceCar Keil / VS Code project.

Run with --baseline only when verifying the original reference import.
Run with --artifacts after a successful Keil build to verify its memory layout.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


FLASH_RANGES = ((0x08000000, 0x08004000), (0x08008000, 0x08020000))
RAM_START, RAM_END = 0x20000000, 0x20010000
REGIONS = {
    "LR_IROM1": (0x08000000, 0x4000),
    "ER_IROM1": (0x08000000, 0x4000),
    "RW_IRAM1": (RAM_START, 0x10000),
    "LR_IROM2": (0x08008000, 0x18000),
    "ER_IROM2": (0x08008000, 0x18000),
}


class VerificationError(Exception):
    """An actionable project or artifact validation failure."""


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def local_path(base, value, boundary, label, directory=False):
    require(isinstance(value, str) and value.strip(), f"{label}: empty path")
    path = (base / value.replace("\\", "/")).resolve()
    require(path.is_relative_to(boundary.resolve()),
            f"{label}: path leaves Firmware (reference MiniBalan must remain independent): {value}")
    exists = path.is_dir() if directory else path.is_file()
    require(exists, f"{label}: missing {'directory' if directory else 'file'}: {path}")
    return path


def node_text(node, path):
    return (node.findtext(path) or "").strip()


def numeric(value, label):
    try:
        return int(value, 0)
    except (ValueError, TypeError) as exc:
        raise VerificationError(f"{label}: invalid integer {value!r}") from exc


def check_manifest(root, firmware):
    path = root / "docs/reference-import.json"
    require(path.is_file(), f"Missing import manifest: {path}")
    manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    require(isinstance(manifest, dict), "Import manifest must be a JSON object")
    imports = manifest.get("imports")
    sources = manifest.get("active_sources")
    require(isinstance(imports, list) and imports, "Manifest imports must be a nonempty list")
    require(isinstance(sources, list) and len(sources) == 73,
            "Manifest active_sources must list the 73 original build inputs")
    imported = {}
    for item in imports:
        require(isinstance(item, dict), "Each imports entry must be an object")
        path = local_path(firmware, item.get("path"), firmware, "Manifest import")
        digest = item.get("sha256", "")
        require(isinstance(digest, str) and re.fullmatch(r"[0-9a-fA-F]{64}", digest),
                f"Manifest import has invalid SHA-256: {item.get('path')}")
        require(path not in imported, f"Duplicate manifest import: {path}")
        imported[path] = digest.lower()
    active = [local_path(firmware, value, firmware, "Manifest source") for value in sources]
    require(len(set(active)) == len(active), "Duplicate manifest active_sources entries")
    require(set(active).issubset(imported), "Some active_sources are absent from imports")
    return manifest, imported, set(active)


def check_project(root, baseline=False):
    firmware = root / "Firmware"
    manifest, imported, active = check_manifest(root, firmware) if baseline else (None, None, set())
    project = firmware / "MDK-ARM/BalanceCar.uvprojx"
    require(project.is_file(), f"Missing Keil project: {project}")
    document = ET.parse(project).getroot()
    targets = document.findall("./Targets/Target")
    require(len(targets) == 1 and node_text(targets[0], "TargetName") == "BalanceCar",
            "Keil project must contain the single BalanceCar target")
    target = targets[0]
    require(node_text(target, "uAC6") == "1" and
            "ARMCLANG" in node_text(target, "pCCUsed").upper(),
            "BalanceCar must use Arm Compiler 6 (ARMCLANG)")
    common = target.find("./TargetOption/TargetCommonOption")
    arm = target.find("./TargetOption/TargetArmAds")
    require(common is not None and arm is not None, "Keil target options are incomplete")
    require(node_text(common, "Device") == "STM32F401RBTx", "Unexpected MCU; expected STM32F401RBTx")
    require("Cortex-M4" in node_text(common, "Cpu") and "FPU2" in node_text(common, "Cpu"),
            "Expected Cortex-M4 with hardware floating point in target CPU settings")
    require(node_text(common, "OutputName") == "BalanceCar", "OutputName must be BalanceCar")
    require(node_text(common, "CreateExecutable") == "1" and node_text(common, "CreateHexFile") == "1",
            "Keil executable and Intel HEX generation must both be enabled")
    output = (project.parent / node_text(common, "OutputDirectory").replace("\\", "/")).resolve()
    require(output == (firmware / "build/keil").resolve(),
            "Keil OutputDirectory must resolve to Firmware/build/keil")
    defines = set(filter(None, re.split(r"[,;\s]+", node_text(arm, "Cads/VariousControls/Define"))))
    require({"USE_HAL_DRIVER", "STM32F401xC"}.issubset(defines),
            "Missing required USE_HAL_DRIVER or STM32F401xC definition")
    other_devices = {name for name in defines if re.match(r"STM32F\d", name) and name != "STM32F401xC"}
    require(not other_devices, f"Conflicting STM32 device definitions: {sorted(other_devices)}")
    include_paths = node_text(arm, "Cads/VariousControls/IncludePath").split(";")
    require(any(value.strip() for value in include_paths), "C compiler include paths are empty")
    includes = set()
    for value in include_paths:
        if value.strip():
            includes.add(local_path(project.parent, value.strip(), firmware, "C include", directory=True))
    entries = target.findall("./Groups/Group/Files/File")
    require(entries, "Keil project contains no build inputs")
    sources = [local_path(project.parent, node_text(entry, "FilePath"), firmware, "Keil source")
               for entry in entries]
    require(len(set(sources)) == len(sources), "Keil project contains duplicate source entries")
    if baseline:
        require(len(entries) == 73, f"Baseline expected 73 build inputs; found {len(entries)}")
        require(sum(node_text(entry, "FileType") == "1" for entry in entries) == 72 and
                sum(node_text(entry, "FileType") == "2" for entry in entries) == 1,
                "Baseline expected 72 C inputs and one assembly startup input")
        missing, extra = active - set(sources), set(sources) - active
        require(not missing and not extra,
                f"Keil source list differs from baseline manifest; missing={sorted(map(str, missing))}; "
                f"extra={sorted(map(str, extra))}")
        reference = root / "MiniBalan"
        reference_project = reference / "MDK-ARM/MiniBalan.uvprojx"
        require(reference_project.is_file(), f"Missing reference project: {reference_project}")
        controls = ET.parse(reference_project).getroot().find(
            "./Targets/Target/TargetOption/TargetArmAds/Cads/VariousControls")
        require(controls is not None, "Reference compiler options are incomplete")
        original_defines = set(filter(None, re.split(r"[,;\s]+", node_text(controls, "Define"))))
        require(defines == original_defines, "Compiler definitions differ from original import baseline")
        original_includes = set()
        for value in node_text(controls, "IncludePath").split(";"):
            if value.strip():
                original = local_path(reference_project.parent, value.strip(), reference,
                                      "Reference C include", directory=True)
                original_includes.add((firmware / original.relative_to(reference.resolve())).resolve())
        require(includes == original_includes, "Compiler include paths differ from original import baseline")
    memories = arm.find("ArmAdsMisc/OnChipMemories")
    require(memories is not None, "Missing Keil memory definitions")
    expected = {"IRAM": (RAM_START, 0x10000), "IROM": (0x08000000, 0x20000),
                "OCR_RVCT4": REGIONS["LR_IROM1"], "OCR_RVCT5": REGIONS["LR_IROM2"],
                "OCR_RVCT9": REGIONS["RW_IRAM1"]}
    for name, limits in expected.items():
        actual = (numeric(node_text(memories, f"{name}/StartAddress"), f"{name} address"),
                  numeric(node_text(memories, f"{name}/Size"), f"{name} size"))
        require(actual == limits,
                f"Keil {name} memory mismatch: expected address={limits[0]:#010x}, size={limits[1]:#x}; "
                f"found address={actual[0]:#010x}, size={actual[1]:#x}")
    require(node_text(arm, "LDads/useFile") == "1", "Keil must use the explicit scatter file")
    scatter = local_path(project.parent, node_text(arm, "LDads/ScatterFile"), firmware, "Scatter")
    require(scatter == (firmware / "MDK-ARM/STM32F401RB.sct").resolve(), "Unexpected scatter file")
    text = re.sub(r";[^\n]*", "", scatter.read_text(encoding="utf-8-sig"))
    declarations = re.findall(r"^\s*(\w+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*\{", text, re.M)
    regions = {name: (int(base, 16), int(size, 16)) for name, base, size in declarations}
    require(len(declarations) == len(REGIONS) and regions == REGIONS,
            "Scatter regions must preserve Sector 1, 128 KiB Flash limit, and 64 KiB RAM limit")
    require(re.search(r"\(\s*RESET\s*,\s*\+First\s*\)", text, re.I),
            "Scatter must place the RESET vector section first")
    print(f"PASS project: {len(entries)} local inputs, ARMCLANG / STM32F401RB, explicit protected Flash layout")
    return firmware, manifest, imported


def check_baseline(root, manifest, imported):
    for path, expected in imported.items():
        require(sha256_file(path) == expected, f"Imported file differs from original baseline: {path}")
    reference = root / "MiniBalan"
    require(reference.is_dir(), f"Missing reference directory: {reference}")
    tree = hashlib.sha256()
    count = 0
    for path in sorted(reference.rglob("*")):
        if path.is_file():
            count += 1
            relative = path.relative_to(reference).as_posix()
            tree.update((relative + "\0" + sha256_file(path) + "\n").encode("utf-8"))
    require(count == manifest.get("reference_file_count"),
            f"Reference file count changed: expected {manifest.get('reference_file_count')}, found {count}")
    require(tree.hexdigest() == manifest.get("reference_tree_sha256"),
            "Reference MiniBalan tree SHA-256 changed since import")
    print(f"PASS baseline: {len(imported)} imported files match; {count} reference files unchanged")


def in_flash(address, size=1):
    return any(start <= address and address + size <= end for start, end in FLASH_RANGES)


def parse_hex(path):
    memory = {}
    upper = 0
    eof = False
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        label = f"HEX line {line_number}"
        require(not eof, f"{label}: record after EOF")
        require(line.startswith(":"), f"{label}: missing ':' prefix")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise VerificationError(f"{label}: invalid hexadecimal record") from exc
        require(len(record) >= 5, f"{label}: record too short")
        size, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        require(len(record) == size + 5, f"{label}: byte count mismatch")
        require(sum(record) & 0xFF == 0, f"{label}: checksum mismatch")
        data = record[4:-1]
        if kind == 0:
            require(address + size <= 0x10000, f"{label}: data crosses a 64 KiB record boundary")
            absolute = upper + address
            require(size == 0 or in_flash(absolute, size),
                    f"{label}: data at {absolute:#010x}..{absolute + size:#010x} enters "
                    "PID Sector 1 or lies outside the 128 KiB MCU Flash")
            for offset, value in enumerate(data):
                key = absolute + offset
                require(key not in memory or memory[key] == value,
                        f"{label}: conflicting data at {key:#010x}")
                memory[key] = value
        elif kind == 1:
            require(size == 0 and address == 0, f"{label}: malformed EOF")
            eof = True
        elif kind in (2, 4):
            require(size == 2 and address == 0, f"{label}: malformed extended address")
            upper = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5):
            require(size == 4 and address == 0, f"{label}: malformed start address")
        else:
            raise VerificationError(f"{label}: unsupported record type {kind:#x}")
    require(eof, "HEX is missing its EOF record")
    require(memory, "HEX contains no firmware data")
    require(all(address in memory for address in range(0x08000000, 0x08000008)),
            "HEX is missing the initial MSP / reset vector")
    msp, reset = struct.unpack("<II", bytes(memory[address] for address in range(0x08000000, 0x08000008)))
    require(RAM_START < msp <= RAM_END and msp % 8 == 0,
            f"Initial MSP {msp:#010x} must be 8-byte aligned and within the 64 KiB RAM")
    require(reset & 1 and in_flash(reset & ~1), f"Invalid Thumb reset vector: {reset:#010x}")
    require((reset & ~1) in memory and ((reset & ~1) + 1) in memory,
            f"Reset handler {reset:#010x} does not point to data present in HEX")
    return memory, msp, reset


def check_map(path):
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    records = re.findall(
        r"^\s*(Load|Execution) Region (\w+) \((?:Base|Exec base): (0x[0-9a-fA-F]+), "
        r"(?:Load base: 0x[0-9a-fA-F]+, )?Size: (0x[0-9a-fA-F]+), Max: (0x[0-9a-fA-F]+),",
        text, re.M)
    found = {}
    for kind, name, base, size, maximum in records:
        require(name in REGIONS, f"MAP has unexpected {kind.lower()} region {name}")
        require(name not in found, f"MAP has duplicate region {name}")
        base, size, maximum = int(base, 16), int(size, 16), int(maximum, 16)
        require((base, maximum) == REGIONS[name], f"MAP {name} base or maximum differs from protected layout")
        require(0 < size <= maximum, f"MAP {name} size {size:#x} exceeds limit {maximum:#x} or is empty")
        found[name] = size
    require(set(found) == set(REGIONS), "MAP does not contain all five expected load/execution regions")
    return found


def check_artifacts(firmware):
    output = firmware / "build/keil"
    paths = {suffix: output / f"BalanceCar.{suffix}" for suffix in ("hex", "axf", "map")}
    for path in paths.values():
        require(path.is_file() and path.stat().st_size > 0, f"Missing or empty build artifact: {path}")
    with paths["axf"].open("rb") as stream:
        require(stream.read(4) == b"\x7fELF", "BalanceCar.axf is not an ELF executable")
    memory, msp, reset = parse_hex(paths["hex"])
    regions = check_map(paths["map"])
    for name in ("LR_IROM1", "LR_IROM2"):
        base = REGIONS[name][0]
        require(all(base <= address < base + regions[name] for address in memory
                    if REGIONS[name][0] <= address < REGIONS[name][0] + REGIONS[name][1]),
                f"HEX has data beyond the actual MAP {name} load size")
        require(base in memory, f"HEX is missing the start of MAP {name}")
    print(f"PASS artifacts: HEX {len(memory)} unique data bytes; MSP={msp:#010x}; reset={reset:#010x}")
    print(f"  Flash regions: {regions['LR_IROM1']}/{0x4000} + {regions['LR_IROM2']}/{0x18000} bytes; "
          f"RAM: {regions['RW_IRAM1']}/{0x10000} bytes; PID Sector 1 has no HEX data")
    print(f"  HEX SHA-256: {sha256_file(paths['hex'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="Project root (defaults to the directory above tools)")
    parser.add_argument("--baseline", action="store_true", help="Verify original import and reference tree hashes")
    parser.add_argument("--artifacts", action="store_true", help="Validate Keil HEX, AXF, and MAP outputs")
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        firmware, manifest, imported = check_project(root, baseline=args.baseline)
        if args.baseline:
            check_baseline(root, manifest, imported)
        if args.artifacts:
            check_artifacts(firmware)
    except (VerificationError, OSError, ValueError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("Verification passed. This does not establish on-device behavior.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
