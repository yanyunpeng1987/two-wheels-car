#!/usr/bin/env python3
"""Exercise strict method-level DEX equivalence, optionally against a real APK decode.

--snapshot and --decoded refer to the complete prepared/final primary smali roots
(not APK files). No APK is modified and no Android device is used.
"""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HEADER = ".class public LExample;\n.super Ljava/lang/Object;\n.field public static count:I = 0x0\n"
FIRST = """.method public static first()I
    .locals 1
    .line 10
    const/4 v0, 0x1
    goto :end
    :other
    const/4 v0, 0x2
    :end
    .line 11
    return v0
.end method
"""
SECOND = """.method public second()V
    .locals 0
    .line 20
    return-void
.end method
"""
SWITCH = """.method public static choose(I)V
    .locals 0
    packed-switch p0, :data
    return-void
    :case
    return-void
    :data
    .packed-switch 0x0
        :case
    .end packed-switch
.end method
"""


def fixtures(equivalence, normalize) -> int:
    before = HEADER + FIRST + SECOND
    positives = {
        "method declaration order": HEADER + SECOND + FIRST,
        "existing zero default rule": before.replace("count:I = 0x0", "count:I"),
        "same-address debug and label": before.replace(":end\n    .line 11", ".line 11\n    :end"),
        "equivalent goto width": before.replace("goto :end", "goto/16 :end"),
    }
    for label, after in positives.items():
        if not equivalence.equivalent_smali(before, after, normalize):
            raise AssertionError("Rejected valid round-trip: " + label)
    padded = SWITCH.replace("    :data\n    .packed-switch", "    nop\n    :data\n    .packed-switch")
    if not equivalence.equivalent_smali(HEADER + SWITCH, HEADER + padded, normalize):
        raise AssertionError("Rejected unreachable packed-switch alignment nop")

    negatives = {
        "missing method": HEADER + FIRST,
        "added method": before + SECOND.replace("second()V", "extra()V"),
        "renamed method": before.replace("second()V", "renamed()V"),
        "changed prototype": before.replace("second()V", "second(I)V"),
        "changed access flag": before.replace("public second", "private second"),
        "changed instruction": before.replace("const/4 v0, 0x1", "const/4 v0, 0x3"),
        "changed branch destination": before.replace("goto :end", "goto :other"),
        "changed debug line": before.replace(".line 11", ".line 12"),
        "debug moved across opcode": before.replace(".line 10\n    const/4 v0, 0x1",
                                                     "const/4 v0, 0x1\n    .line 10"),
        "changed field": before.replace("count:I = 0x0", "count:I = 0x1"),
        "changed superclass": before.replace("Ljava/lang/Object;", "LOther;"),
        "duplicate method": before + SECOND,
        "unterminated method": before.rsplit(".end method", 1)[0],
        "ordinary executable nop": before.replace("    return v0", "    nop\n    return v0"),
        "method annotation changed": before.replace("    .locals 0", "    .annotation runtime LChanged;\n    .end annotation\n    .locals 0"),
    }
    for label, after in negatives.items():
        if equivalence.equivalent_smali(before, after, normalize):
            raise AssertionError("Accepted non-equivalent or malformed class: " + label)
    for label, payload in {
        "labelled nop": padded.replace("    nop\n", "    :addressed_nop\n    nop\n"),
        "multiple alignment nops": padded.replace("    nop\n", "    nop\n    nop\n"),
        "changed switch value": padded.replace(".packed-switch 0x0", ".packed-switch 0x1"),
    }.items():
        if equivalence.equivalent_smali(HEADER + SWITCH, HEADER + payload, normalize):
            raise AssertionError("Accepted invalid payload normalization: " + label)
    reachable = SWITCH.replace("    :case\n    return-void", "    :case\n    move p0, p0")
    reachable_padding = reachable.replace("    :data\n    .packed-switch",
                                         "    nop\n    :data\n    .packed-switch")
    if equivalence.equivalent_smali(HEADER + reachable, HEADER + reachable_padding, normalize):
        raise AssertionError("Discarded a reachable nop before switch data")
    return len(positives) + 1 + len(negatives) + 4


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--decoded", type=Path)
    args = parser.parse_args()
    if (args.snapshot is None) != (args.decoded is None):
        parser.error("--snapshot and --decoded must be supplied together")
    module = Path(__file__).resolve().parents[2]
    helper = load("ui_normalizer", module / "tools/Apply-UiPatch.py")
    equivalence = load("method_equivalence", module / "tools/smali_equivalence.py")
    count = fixtures(equivalence, helper.normalize_smali)
    print(f"DEX round-trip equivalence fixtures: PASS ({count} positive/negative cases)")
    if args.snapshot is None:
        return
    expected = {p.relative_to(args.snapshot).as_posix(): p for p in args.snapshot.rglob("*.smali")}
    actual = {p.relative_to(args.decoded).as_posix(): p for p in args.decoded.rglob("*.smali")}
    if not expected or expected.keys() != actual.keys():
        raise AssertionError("Nonempty complete primary class sets must match")
    changed_order = []
    failures = []
    for name, path in expected.items():
        before = path.read_text(encoding="utf-8")
        after = actual[name].read_text(encoding="utf-8")
        if not equivalence.equivalent_smali(before, after, helper.normalize_smali):
            failures.append(name)
        elif helper.normalize_smali(before) != helper.normalize_smali(after):
            changed_order.append(name)
    if failures:
        raise AssertionError("Compiled class content differs: " + ", ".join(failures[:20]))
    print(f"Real DEX complete class comparison: PASS ({len(expected)} classes; "
          f"{len(changed_order)} require observed method/debug/padding equivalence)")


if __name__ == "__main__":
    main()
