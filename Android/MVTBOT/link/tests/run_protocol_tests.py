#!/usr/bin/env python3
"""Run the production byte-stream parser and validation on the host JVM."""
import argparse
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java-home", type=Path, required=True)
    args = parser.parse_args()
    link = Path(__file__).resolve().parents[1]
    suffix = ".exe" if (args.java_home / "bin/javac.exe").is_file() else ""
    sources = [link / "src/com/mvtbot/link" / name
               for name in ("FrameDecoder.java", "ProtocolValidation.java")]
    sources.append(link / "test/com/mvtbot/link/ProtocolTest.java")
    with tempfile.TemporaryDirectory(prefix="mvtbot-protocol-test-") as directory:
        subprocess.run([str(args.java_home / ("bin/javac" + suffix)), "--release", "8",
                        "-encoding", "UTF-8", "-d", directory, *map(str, sources)], check=True)
        subprocess.run([str(args.java_home / ("bin/java" + suffix)), "-cp", directory,
                        "com.mvtbot.link.ProtocolTest"], check=True)


if __name__ == "__main__":
    main()
