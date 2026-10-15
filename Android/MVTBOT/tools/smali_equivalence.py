"""Strict class/method comparison for the observed LINK DEX round trip.

The assembler orders methods by DEX method IDs, so source declaration order is
not retained after adding wrappers. Every method signature and complete body is
still checked. The caller supplies the existing, independently tested v21/UI
normalizer for label names, encoding widths and static default values.

Two additional transformations are limited to differences observed in the
combined v24 APK: moving a .line directive past labels at the same code address,
and a single unreachable nop inserted immediately before packed-switch data.
All executable instructions, annotations, access flags, fields and debug values
remain checked.
"""
from __future__ import annotations

import re
from collections.abc import Callable


class SmaliFormatError(ValueError):
    """Malformed or duplicate method declarations must fail closed."""


LABEL = re.compile(r":[A-Za-z_][A-Za-z_0-9]*")
LINE = re.compile(r"\.line [0-9]+")
METHOD_SIGNATURE = re.compile(r"[^\s(]+\([^\s)]*\)[^\s]+")
TERMINATOR = re.compile(r"(?:return-void|return(?:-object|-wide)? [vp][0-9]+|"
                        r"throw [vp][0-9]+|goto :[A-Za-z_][A-Za-z_0-9]*)")


def split_class(source: str) -> tuple[str, dict[str, str]]:
    """Keep nonmethod declarations verbatim; index full methods by name/prototype."""
    outside: list[str] = []
    methods: dict[str, str] = {}
    current: list[str] | None = None
    signature: str | None = None
    class_count = 0
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith(".class "):
            class_count += 1
        if stripped.startswith(".method "):
            if current is not None:
                raise SmaliFormatError("Nested method declaration")
            signature = stripped.split()[-1]
            if not METHOD_SIGNATURE.fullmatch(signature) or signature in methods:
                raise SmaliFormatError("Invalid or duplicate method signature: " + signature)
            current = [line]
        elif stripped == ".end method":
            if current is None or signature is None:
                raise SmaliFormatError("Method end without method declaration")
            current.append(line)
            methods[signature] = "\n".join(current)
            current = None
            signature = None
        elif current is not None:
            current.append(line)
        else:
            outside.append(line)
    if current is not None or class_count != 1:
        raise SmaliFormatError("Unterminated method or nonunique class declaration")
    return "\n".join(outside), methods


def normalize_method(method: str, normalize: Callable[[str], str]) -> str:
    lines = normalize(method).splitlines()
    without_padding: list[str] = []
    for index, line in enumerate(lines):
        # This nop is unlabelled and cannot execute: the preceding opcode always
        # terminates/transfers control, and the following label marks switch data.
        # A normal executable nop, labelled nop, second nop, or different payload
        # construct is deliberately not accepted by this narrowly scoped rule.
        if (line == "nop" and index > 0 and index + 2 < len(lines)
                and TERMINATOR.fullmatch(lines[index - 1])
                and LABEL.fullmatch(lines[index + 1])
                and lines[index + 2].startswith(".packed-switch ")):
            continue
        without_padding.append(line)

    result: list[str] = []
    index = 0
    while index < len(without_padding):
        line = without_padding[index]
        if LINE.fullmatch(line) or LABEL.fullmatch(line):
            end = index
            while end < len(without_padding) and (
                    LINE.fullmatch(without_padding[end]) or LABEL.fullmatch(without_padding[end])):
                end += 1
            # Both labels and line mappings consume zero code units. Preserve the
            # relative order of all line values and all labels at this address.
            block = without_padding[index:end]
            result.extend(item for item in block if LINE.fullmatch(item))
            result.extend(item for item in block if LABEL.fullmatch(item))
            index = end
        else:
            result.append(line)
            index += 1
    return "\n".join(result)


def equivalent_smali(expected: str, actual: str, normalize: Callable[[str], str]) -> bool:
    """Compare the complete class, allowing only documented DEX round-trip choices."""
    try:
        before_header, before_methods = split_class(expected)
        after_header, after_methods = split_class(actual)
    except SmaliFormatError:
        return False
    if normalize(before_header) != normalize(after_header):
        return False
    if before_methods.keys() != after_methods.keys():
        return False
    return all(normalize_method(body, normalize) == normalize_method(after_methods[signature], normalize)
               for signature, body in before_methods.items())
