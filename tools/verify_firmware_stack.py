#!/usr/bin/env python3
"""Check the real Keil call graph against the reserved Cortex-M stack.

This is a static minimum gate, not a replacement for on-device high-water checks.
Keil can report unknown indirect-call depths and simultaneous exceptions add use.
"""
import argparse
import json
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    startup = (root / "Firmware/MDK-ARM/startup_stm32f401xc.s").read_text(encoding="utf-8")
    graph = (root / "Firmware/build/keil/BalanceCar.htm").read_text(encoding="utf-8", errors="replace")
    reserved = int(re.search(r"Stack_Size\s+EQU\s+(0x[0-9a-fA-F]+)", startup).group(1), 16)
    depths = {}
    for name in ("main", "EXTI2_IRQHandler", "bluetooth_dma_rx_callback"):
        block = re.search(r'<P><STRONG><a name="[^"]+"></a>' + re.escape(name)
                          + r'</STRONG>.*?(?=<P><STRONG>|\Z)', graph, re.S)
        if not block:
            raise ValueError(f"Missing function in real linker call graph: {name}")
        depth = re.search(r"Max Depth =\s*(\d+)", block.group())
        if not depth:
            raise ValueError(f"Missing stack depth: {name}")
        depths[name] = int(depth.group(1))
    # Main + preempting control path, Cortex-M/FPU exception frame, alignment,
    # and explicit margin. Current peripheral IRQs share preemption priority 0.
    known_requirement = depths["main"] + max(depths["EXTI2_IRQHandler"],
                                            depths["bluetooth_dma_rx_callback"]) + 112
    if reserved < 4096 or known_requirement + 512 > reserved:
        raise ValueError(f"Insufficient stack: reserved={reserved}, known+exception={known_requirement}")
    report = {"status": "PASS", "reservedBytes": reserved, "callGraphDepths": depths,
              "knownDepthWithExceptionBytes": known_requirement,
              "unknownIndirectDepthsRemain": "Unknown" in graph,
              "onDeviceHighWaterVerified": False}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"PASS: stack {reserved} bytes; known call chains plus exception {known_requirement} bytes")
    print("Indirect paths and actual high-water usage still require device validation.")


if __name__ == "__main__":
    main()
