#!/usr/bin/env python3
"""Decode Box0 serial frames to PNG (requires Pillow).

Usage: python firmware/tests/decode_box0_capture.py capture.log -o output_dir
Names preserve frame order, including gaps for skipped frames. Exit status is
1 if any frame was skipped or no complete frames were found; otherwise 0.
"""

import argparse
from collections.abc import Iterable
from pathlib import Path
import re
import sys

from PIL import Image


def rgb565_to_rgb(payload: str) -> bytes:
    """Pixels are four hexadecimal digits each, most significant digit first."""
    packed = bytes.fromhex(payload)
    rgb = bytearray()
    for offset in range(0, len(packed), 2):
        pixel = (packed[offset] << 8) | packed[offset + 1]
        red = (pixel >> 11) & 31
        green = (pixel >> 5) & 63
        blue = pixel & 31
        rgb.extend(((red << 3) | (red >> 2),
                    (green << 2) | (green >> 4),
                    (blue << 3) | (blue >> 2)))
    return bytes(rgb)


def decode_capture(lines: Iterable[str], output_dir: Path) -> tuple[int, int]:
    output_dir.mkdir(parents=True, exist_ok=True)
    frame_number = saved = skipped = 0
    active = False
    width = height = 0
    rows: dict[int, str] = {}
    problems: list[str] = []

    def finish(reason: str | None = None) -> None:
        nonlocal saved, skipped
        if reason:
            problems.append(reason)
        if height > len(rows):
            problems.append(f"missing {height - len(rows)} of {height} rows")
        if problems:
            skipped += 1
            print(f"Frame {frame_number}: skipped: {'; '.join(problems)}", file=sys.stderr)
            return
        rgb = b"".join(rgb565_to_rgb(rows[y]) for y in range(height))
        destination = output_dir / f"frame_{frame_number:04d}.png"
        with Image.frombytes("RGB", (width, height), rgb) as image:
            image.save(destination)
        saved += 1
        print(f"Frame {frame_number}: saved {destination} ({width}x{height})")

    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.rstrip("\r\n")
        if line.startswith("[FRAME]"):
            if active:
                finish("missing [FRAME_END] before next [FRAME]")
            frame_number += 1
            active = True
            rows = {}
            problems = []
            width = height = 0
            match = re.fullmatch(r"\[FRAME\] (\d+) (\d+)", line)
            if match:
                width, height = map(int, match.groups())
            if width <= 0 or height <= 0:
                problems.append(f"line {line_number}: invalid frame dimensions/header")
        elif active and line.startswith("[ROW]"):
            match = re.fullmatch(r"\[ROW\] (-?\d+) (.*)", line)
            if not match:
                problems.append(f"line {line_number}: malformed row")
                continue
            y = int(match[1])
            payload = match[2]
            if not 0 <= y < height:
                problems.append(f"line {line_number}: row {y} outside height {height}")
            elif y in rows:
                problems.append(f"line {line_number}: duplicate row {y}")
            elif len(payload) != width * 4:
                problems.append(f"line {line_number}: row {y} has {len(payload)} hex characters, expected {width * 4}")
            elif not re.fullmatch(r"[0-9a-fA-F]+", payload):
                problems.append(f"line {line_number}: row {y} contains non-hex characters")
            else:
                rows[y] = payload
        elif active and line == "[FRAME_END]":
            finish()
            active = False

    if active:
        finish("missing [FRAME_END] at end of log")
    print(f"Decoded {saved} frame(s); skipped {skipped}.")
    return saved, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="capture_box0.ps1 serial log")
    parser.add_argument("-o", "--output-dir", type=Path, required=True, help="PNG output directory")
    args = parser.parse_args()
    try:
        with args.log.open(encoding="utf-8-sig", errors="replace") as log:
            saved, skipped = decode_capture(log, args.output_dir)
    except OSError as error:
        parser.exit(2, f"error: {error}\n")
    if not saved:
        print("No complete frames found.", file=sys.stderr)
    return 1 if skipped or not saved else 0


if __name__ == "__main__":
    raise SystemExit(main())
