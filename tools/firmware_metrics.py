#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


SECTION_RE = re.compile(r"^(?P<name>\.\S+)\s+(?P<size>\d+)\s+(?P<address>\d+)$")
SUMMARY_RE = re.compile(
    r"^\s*(?P<text>\d+)\s+(?P<data>\d+)\s+(?P<bss>\d+)\s+(?P<dec>\d+)\s+(?P<hex>[0-9a-fA-F]+)\s+"
)


def run_size(tool: str, image: Path, sections: bool) -> str:
    command = [tool]
    if sections:
        command.append("-A")
    command.append(str(image))
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def parse_sections(output: str) -> dict[str, int]:
    sections: dict[str, int] = {}
    for line in output.splitlines():
        match = SECTION_RE.match(line.strip())
        if match:
            sections[match.group("name")] = int(match.group("size"))
    return sections


def parse_summary(output: str) -> dict[str, int]:
    for line in output.splitlines():
        match = SUMMARY_RE.match(line)
        if match:
            return {
                "text": int(match.group("text")),
                "data": int(match.group("data")),
                "bss": int(match.group("bss")),
                "total": int(match.group("dec")),
            }
    raise ValueError("arm-none-eabi-size summary was not found")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Record reproducible firmware ELF size metrics as JSON.")
    parser.add_argument("image", type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--ref", default="")
    parser.add_argument("--size-tool", default="arm-none-eabi-size")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = args.image.resolve()
    result = {
        "schemaVersion": 1,
        "label": args.label,
        "ref": args.ref,
        "image": image.name,
        "summary": parse_summary(run_size(args.size_tool, image, sections=False)),
        "sections": parse_sections(run_size(args.size_tool, image, sections=True)),
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
