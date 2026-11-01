#!/usr/bin/env python3
"""Generate a test-only Box0 sketch without editing production sources.

Run: python firmware/tests/prepare_box0_probe.py
Output: firmware/.build/box0-probe/MotoNav (compile/flash separately).
Serial: STATUS\n or CAPTURE\n; the latter emits numeric RGB565 pixels as
[FRAME] 240 240, 240 [ROW] y <960 hex digits> lines, then [FRAME_END].
The guarded canvas and heap are checked every five seconds. Start with live
phone navigation. Optional HOME/MUSIC/WEATHER/PAIR/NAV/MAP previews are manual;
live data can redraw them. Send e.g. PAIR\nCAPTURE\n together for a preview.
NAV supplies sample direction/camera/lanes/text, overwriting navigation fields
and slots 0/1; MAP re-enables the real map without fabricating or restoring data.
No command changes BLE authentication; PAIR draws a labeled dummy code only.
"""

from pathlib import Path
import re
import shutil


def replace_once(text: str, pattern: str, replacement, description: str) -> str:
    """Fail before writing if the source no longer has a unique injection point."""
    result, count = re.subn(pattern, replacement, text, flags=re.MULTILINE)
    if count != 1:
        raise ValueError(f"Expected one {description}, found {count}")
    return result


def instrument_sketch(source: str, header: str) -> str:
    source = replace_once(
        source,
        r'^(#define\s+FW_VERSION\s+")([^"\n]+)("[^\n]*)$',
        lambda match: match[1] + match[2].removesuffix("-PROBE") + "-PROBE" + match[3],
        "FW_VERSION definition",
    )
    source = replace_once(
        source,
        r'(^[ \t]*Serial\.println\(String\("MotoNav ready, firmware "\)'
        r' \+ FW_VERSION\);[ \t]*\n)([ \t]*\})',
        lambda match: match[1] + "  probeInit();\n" + match[2],
        "setup ready log followed by its closing brace",
    )
    source = replace_once(
        source,
        r"^void loop\(\) \{[ \t]*\n",
        lambda match: match[0] + "  probeTick();\n",
        "loop definition",
    )
    return replace_once(
        source,
        r"^void setup\(\) \{",
        lambda match: "// BEGIN test-only Box0 render probe\n"
        + header.rstrip()
        + "\n// END test-only Box0 render probe\n\n"
        + match[0],
        "setup definition",
    )


def prepare_probe(firmware: Path) -> Path:
    source_dir = firmware / "boards" / "box0" / "MotoNav"
    header = (firmware / "tests" / "Box0RenderProbe.h").read_text(encoding="utf-8")
    sketch = instrument_sketch(
        (source_dir / "MotoNav.ino").read_text(encoding="utf-8"), header
    )
    output_dir = firmware / ".build" / "box0-probe" / "MotoNav"
    output_dir.mkdir(parents=True, exist_ok=True)
    for source in sorted(source_dir.iterdir()):
        if source.is_file() and source.suffix.lower() in {".ino", ".h"}:
            shutil.copy2(source, output_dir / source.name)
    (output_dir / "MotoNav.ino").write_text(sketch, encoding="utf-8", newline="\n")
    return output_dir


if __name__ == "__main__":
    try:
        output = prepare_probe(Path(__file__).resolve().parents[1])
    except (OSError, ValueError) as error:
        raise SystemExit(f"Box0 probe generation failed: {error}") from error
    print(f"Prepared -PROBE sketch: {output}")
    print("No compilation, flashing, or serial access performed.")
