#!/usr/bin/env python3
"""Generate VS Code IntelliSense settings from the authoritative Keil target.

This intentionally supports this project's ArmClang Cortex-M4 / C99 target.
It fails on compiler overrides rather than silently giving the editor a
different view of the code. No packages beyond the Python standard library.
"""

import argparse
import json
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


def required_text(node, path):
    value = node.findtext(path)
    if value is None or not value.strip():
        raise ValueError("Missing project setting: " + path)
    return value.strip()


def split_values(value, separators):
    """Split Keil lists without breaking separators inside quoted values."""
    values, current, quoted = [], [], False
    for char in value:
        if char == '"':
            quoted = not quoted
        if char in separators and not quoted:
            if "".join(current).strip():
                values.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    if quoted:
        raise ValueError("Unbalanced quotes in Keil setting: " + value)
    if "".join(current).strip():
        values.append("".join(current).strip())
    return values


def generate(workspace, keil_root):
    project_path = workspace / "Firmware/MDK-ARM/BalanceCar.uvprojx"
    project = ET.parse(project_path).getroot()
    targets = [t for t in project.findall("Targets/Target")
               if t.findtext("TargetName") == "BalanceCar"]
    if len(targets) != 1:
        raise ValueError("Expected exactly one target named BalanceCar")
    target = targets[0]
    if target.findtext("uAC6") != "1":
        raise ValueError("Only Arm Compiler 6 targets are supported")
    arm = target.find("TargetOption/TargetArmAds")
    if arm is None:
        raise ValueError("Missing TargetArmAds settings")
    if required_text(arm, "ArmAdsMisc/AdsCpuType").strip('"') != "Cortex-M4":
        raise ValueError("Only Cortex-M4 is supported; extend the generator first")
    if arm.findtext("ArmAdsMisc/RvdsVP") != "2":
        raise ValueError("Expected single-precision hardware FPU (RvdsVP=2)")
    cads = arm.find("Cads")
    if cads is None or cads.findtext("v6Lang") != "3":
        raise ValueError("Expected ArmClang C99 (v6Lang=3)")
    if cads.findtext("v6LangP") != "3":
        raise ValueError("Expected ArmClang C++11 (v6LangP=3)")
    for group in target.findall("Groups/Group"):
        # Keil can alter macros, includes, language or build inclusion here.
        # Supporting inheritance correctly needs an explicit future extension.
        if group.find("GroupOption") is not None:
            raise ValueError("Unsupported group options: " + required_text(group, "GroupName"))
        for source in group.findall("Files/File"):
            if source.find("FileOption") is not None:
                raise ValueError("Unsupported file options: " + required_text(source, "FileName"))
    controls = cads.find("VariousControls")
    if controls is None:
        raise ValueError("Missing target compiler controls")
    for field in ("MiscControls", "Undefine"):
        if (controls.findtext(field) or "").strip():
            raise ValueError("Unsupported compiler setting: " + field)

    compiler = keil_root / "ARM/ARMCLANG/bin/armclang.exe"
    if not compiler.is_file():
        raise ValueError("ArmClang not found: " + str(compiler))
    include_paths = []
    for include in split_values(controls.findtext("IncludePath") or "", ";"):
        include = include.strip('"')
        if "$" in include or "%" in include:
            raise ValueError("Keil path variables are not supported: " + include)
        resolved = (project_path.parent / include.replace("\\", "/")).resolve()
        if not resolved.is_dir():
            raise ValueError("Include directory not found: " + str(resolved))
        try:
            rendered = "${workspaceFolder}/" + resolved.relative_to(workspace).as_posix()
        except ValueError:
            rendered = resolved.as_posix()
        if rendered not in include_paths:
            include_paths.append(rendered)
    defines = split_values(controls.findtext("Define") or "", ",;")
    for define in defines:
        if not re.match(r"^[A-Za-z_][A-Za-z_0-9]*(?:=.*)?$", define):
            raise ValueError("Unsupported macro definition: " + define)

    compiler_args = ["--target=arm-arm-none-eabi", "-mcpu=cortex-m4", "-mthumb",
                     "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
    if cads.findtext("vShortEn") == "1":
        compiler_args.append("-fshort-enums")
    if cads.findtext("vShortWch") == "1":
        compiler_args.append("-fshort-wchar")
    return {
        "configurations": [{
            "name": "BalanceCar (Keil Arm Compiler 6)",
            "compilerPath": compiler.resolve().as_posix(),
            "compilerArgs": compiler_args,
            "intelliSenseMode": "windows-clang-arm",
            "includePath": include_paths,
            "defines": defines,
            "cStandard": "c99",
            "cppStandard": "c++11",
            "browse": {"path": include_paths, "limitSymbolsToIncludedHeaders": True},
        }],
        "version": 4,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="Verify generated settings are current without writing")
    parser.add_argument("--keil-root", default=os.environ.get("KEIL_ROOT") or r"C:\Keil_v5",
                        help="Keil installation directory (default: KEIL_ROOT or C:\\Keil_v5)")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parent.parent
    destination = workspace / ".vscode/c_cpp_properties.json"
    try:
        expected = generate(workspace, Path(args.keil_root).resolve())
        if args.check:
            if not destination.is_file() or json.loads(destination.read_text(encoding="utf-8-sig")) != expected:
                print("VS Code settings are stale. Run: python tools/sync_vscode.py", file=sys.stderr)
                return 1
            print("VS Code settings match BalanceCar.uvprojx.")
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(expected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print("Updated .vscode/c_cpp_properties.json from BalanceCar.uvprojx.")
        return 0
    except (OSError, ValueError, ET.ParseError) as error:
        print("Error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
