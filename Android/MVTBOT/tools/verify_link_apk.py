#!/usr/bin/env python3
"""Verify final DEX payload, unique classes, and resolved LINK-001 bridge methods."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from zipfile import ZipFile


class Dex:
    def __init__(self, data):
        self.data = data
        if data[:4] != b"dex\n" or len(data) < 112:
            raise ValueError("Invalid DEX")
        self.method_count = self.u32(88)
        self.strings = []
        for i in range(self.u32(56)):
            pos = self.u32(self.u32(60) + i * 4)
            _, pos = self.uleb(pos)
            self.strings.append(data[pos:data.index(b"\0", pos)].decode("utf-8", errors="replace"))
        self.types = [self.strings[self.u32(self.u32(68) + 4 * i)] for i in range(self.u32(64))]
        self.protos = []
        for i in range(self.u32(72)):
            pos = self.u32(76) + 12 * i
            ret, params = self.u32(pos + 4), self.u32(pos + 8)
            args = "" if not params else "".join(self.types[self.u16(params + 4 + 2*j)]
                                                 for j in range(self.u32(params)))
            self.protos.append("(" + args + ")" + self.types[ret])
        self.methods = []
        for i in range(self.method_count):
            pos = self.u32(92) + 8 * i
            self.methods.append(self.types[self.u16(pos)] + "->" + self.strings[self.u32(pos + 4)]
                                + self.protos[self.u16(pos + 2)])
        self.classes, self.defined_methods = set(), set()
        for i in range(self.u32(96)):
            pos = self.u32(100) + 32 * i
            self.classes.add(self.types[self.u32(pos)])
            cursor = self.u32(pos + 24)
            if not cursor:
                continue
            counts = []
            for _ in range(4):
                n, cursor = self.uleb(cursor)
                counts.append(n)
            for _ in range(counts[0] + counts[1]):
                _, cursor = self.uleb(cursor)
                _, cursor = self.uleb(cursor)
            for count in counts[2:]:
                index = 0
                for _ in range(count):
                    diff, cursor = self.uleb(cursor)
                    index += diff
                    _, cursor = self.uleb(cursor)
                    _, cursor = self.uleb(cursor)
                    self.defined_methods.add(self.methods[index])

    def u16(self, p):
        return struct.unpack_from("<H", self.data, p)[0]

    def u32(self, p):
        return struct.unpack_from("<I", self.data, p)[0]

    def uleb(self, p):
        value, shift = 0, 0
        while True:
            b = self.data[p]
            p += 1
            value |= (b & 127) << shift
            if b < 128:
                return value, p
            shift += 7
            if shift > 35:
                raise ValueError("Invalid ULEB128")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--apk", type=Path, required=True)
    ap.add_argument("--module-root", type=Path, required=True)
    ap.add_argument("--snapshot", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--rollback-ui-only", action="store_true")
    args = ap.parse_args()
    baseline = json.loads((args.module_root / "baseline/project-files.json").read_text(encoding="utf-8"))
    expected = {x["path"]: x["sha256"] for x in baseline["files"] if x["path"].endswith(".dex")}
    classes, dexes, rows = set(), {}, {}
    with ZipFile(args.apk) as z:
        names = z.namelist()
        if len(set(names)) != len(names):
            raise ValueError("Duplicate APK entries")
        dex_names = {n for n in names if n.startswith("classes") and n.endswith(".dex") and "/" not in n}
        required = set(expected) if args.rollback_ui_only else set(expected) | {"classes4.dex"}
        if dex_names != required:
            raise ValueError(f"Unexpected DEX set: {dex_names}")
        for name in sorted(dex_names):
            data = z.read(name)
            digest = hashlib.sha256(data).hexdigest()
            compiled = args.snapshot / name
            if not compiled.is_file():
                compiled = args.snapshot / "build/apk" / name
            if not compiled.is_file() or hashlib.sha256(compiled.read_bytes()).hexdigest() != digest:
                raise ValueError(f"Final APK differs from compiled input: {name}")
            if (name in ("classes2.dex", "classes3.dex")) and digest != expected[name]:
                raise ValueError(f"Preserved baseline DEX changed: {name}")
            d = Dex(data)
            if d.method_count > 65535 or classes & d.classes:
                raise ValueError(f"Duplicate classes or DEX method limit: {name}")
            classes |= d.classes
            dexes[name] = d
            rows[name] = {"sha256": digest, "classes": len(d.classes), "methods": d.method_count}
    if rows["classes.dex"]["sha256"] == expected["classes.dex"]:
        raise ValueError("Primary DEX was not rebuilt")
    if not args.rollback_ui_only:
        required = {f"Lcom/mvtbot/link/{n};" for n in ("MiniBalanLink", "FrameDecoder", "ProtocolValidation")}
        if not required <= dexes["classes4.dex"].classes:
            raise ValueError("New LINK-001 classes are missing")
        refs = {m for m in dexes["classes.dex"].methods if m.startswith("Lcom/mvtbot/link/")}
        if not refs:
            raise ValueError("Main DEX does not reference the LINK-001 bridge")
        unresolved = refs - dexes["classes4.dex"].defined_methods
        if unresolved:
            raise ValueError(f"Unresolved bridge method signatures: {sorted(unresolved)}")
    else:
        refs = set()
    report = {"status": "PASS", "mode": "rollback-ui-only" if args.rollback_ui_only else "hc05d",
              "dex": rows, "resolvedBridgeMethods": sorted(refs), "uniqueClasses": len(classes),
              "runtimeArtVerified": False, "deviceOperations": False}
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"PASS: {len(dexes)} DEX files, {len(refs)} resolved bridge methods, {len(classes)} unique classes")


if __name__ == "__main__":
    main()
