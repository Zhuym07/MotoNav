"""Extract BOX0 production drawing functions and build an Android regression test.

Run with Python; the generated executable must be run separately on an ARM64
Android device. This script never connects to a device or flashes firmware.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_NDK = Path(
    "C:/Users/Zhuym/AppData/Local/Android/Sdk/ndk/27.0.12077973"
)


def balanced_definition(source, pattern):
    """Find a definition, balancing braces outside comments and quoted text."""
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError(f"Missing production definition: {pattern}")
    depth, state, i = 1, "code", match.end()
    while i < len(source):
        c, following = source[i], source[i:i + 2]
        if state == "line":
            if c == "\n":
                state = "code"
        elif state == "block":
            if following == "*/":
                state = "code"
                i += 1
        elif state in ('"', "'"):
            if c == "\\":
                i += 1
            elif c == state:
                state = "code"
        elif following in ("//", "/*"):
            state = "line" if following == "//" else "block"
            i += 1
        elif c in ('"', "'"):
            state = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                line = source.count("\n", 0, match.start()) + 1
                return line, source[match.start():i + 1]
        i += 1
    raise ValueError(f"Unclosed production definition: {pattern}")


def function(source, name):
    return balanced_definition(
        source,
        rf"^[ \t]*(?:(?:static|inline)\s+)*(?:void|uint16_t)\s+"
        rf"{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{",
    )


SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <vector>
using std::min;
using std::max;
template<class T> T constrain(T x, T a, T b) { return min(max(x, a), b); }
constexpr float PI = 3.14159265358979323846f;
#define C_BLACK 0
constexpr size_t PIXELS = LCD_WIDTH * LCD_HEIGHT;
// The old -92..333 loop fits in these guards, making corruption deterministic
// instead of relying on an allocator or a segmentation fault to catch it.
constexpr size_t GUARD = 32768;
constexpr uint16_t SENTINEL = 0xa55a;
std::vector<uint16_t> storage(PIXELS + 2 * GUARD);
uint16_t *pixels = storage.data() + GUARD;
struct MockCanvas {
  uint16_t *framebuffer = pixels;
  uint16_t *getFramebuffer() { return framebuffer; }
  void drawFastHLine(int x, int y, int w, uint16_t color) {
    // Do not silently clip: caller bounds are the behavior being tested.
    assert(framebuffer && x >= 0 && y >= 0 && y < LCD_HEIGHT);
    assert(w >= 0 && x <= LCD_WIDTH && w <= LCD_WIDTH - x);
    std::fill(framebuffer + y * LCD_WIDTH + x,
              framebuffer + y * LCD_WIDTH + x + w, color);
  }
} canvas;
MockCanvas *gfx = &canvas;
void reset() {
  std::fill(storage.begin(), storage.end(), SENTINEL);
  std::fill(pixels, pixels + PIXELS, 0xffff);
  canvas.framebuffer = pixels;
}
void guards() {
  for (size_t i = 0; i < GUARD; ++i) {
    assert(storage[i] == SENTINEL);
    assert(storage[GUARD + PIXELS + i] == SENTINEL);
  }
}
void unchanged() {
  guards();
  assert(std::all_of(pixels, pixels + PIXELS,
                    [](uint16_t c) { return c == 0xffff; }));
}
'''


TESTS = r'''
int calls, firstRow, lastRow, rowStep;
void captureRows(int y0, int y1, int step) {
  ++calls; firstRow = y0; lastRow = y1; rowStep = step;
  assert(y0 >= 0 && y0 < y1 && y1 <= LCD_HEIGHT && step == 1);
  for (int y = y0; y < y1; y += step)
    std::fill(pixels + y * LCD_WIDTH, pixels + (y + 1) * LCD_WIDTH, 0);
}
void testRows() {
  reset(); calls = 0;
  parallelRows(captureRows, -92, 333);
  assert(calls == 1 && firstRow == 0 && lastRow == LCD_HEIGHT && rowStep == 1);
  guards();
  assert(std::all_of(pixels, pixels + PIXELS, [](uint16_t c) { return c == 0; }));
  reset(); calls = 0;
  parallelRows(captureRows, -92, -1);
  parallelRows(captureRows, 0, 0);
  parallelRows(captureRows, 240, 333);
  parallelRows(captureRows, 200, 100);
  parallelRows(nullptr, -92, 333);
  assert(calls == 0); unchanged();
  parallelRows(captureRows, -92, 4);
  assert(calls == 1 && firstRow == 0 && lastRow == 4);
  assert(pixels[0] == 0 && pixels[4 * LCD_WIDTH] == 0xffff); guards();
  reset(); calls = 0;
  parallelRows(captureRows, 238, 333);
  assert(calls == 1 && firstRow == 238 && lastRow == 240);
  assert(pixels[237 * LCD_WIDTH] == 0xffff && pixels[PIXELS - 1] == 0); guards();
  parallelRows(captureRows, INT_MIN, INT_MAX);
  assert(firstRow == 0 && lastRow == LCD_HEIGHT); guards();
  puts("PASS parallelRows: clipped, empty, outside, null and extreme ranges");
}
void testVignette() {
  reset();
  drawVignette(178, 211, 150); // Historical navigation arguments from the crash path.
  guards();
  assert(pixels[120 * LCD_WIDTH + 120] == 0xffff);
  // White RGB565 retaining 106/256 brightness: (12,26,12).
  assert(pixels[0] == 0x634c && pixels[LCD_WIDTH - 1] == 0x634c);
  assert(pixels[PIXELS - LCD_WIDTH] == 0x634c && pixels[PIXELS - 1] == 0x634c);
  assert(pixels[120 * LCD_WIDTH] == 0x634c);
  assert(pixels[120 * LCD_WIDTH + 33] == 0xffff);
  assert(pixels[120 * LCD_WIDTH + 16] > pixels[120 * LCD_WIDTH]);
  assert(pixels[120 * LCD_WIDTH + 16] < 0xffff);
  for (int y = 0; y < LCD_HEIGHT; ++y) for (int x = 0; x < LCD_WIDTH; ++x) {
    auto c = pixels[y * LCD_WIDTH + x];
    assert(c == pixels[y * LCD_WIDTH + LCD_WIDTH - 1 - x]);
    assert(c == pixels[(LCD_HEIGHT - 1 - y) * LCD_WIDTH + x]);
  }
  reset(); drawVignette(0, 24, 150); guards(); // Native square navigation call.
  assert(pixels[0] == 0x634c && pixels[PIXELS - 1] == 0x634c);
  assert(pixels[120 * LCD_WIDTH + 24] == 0xffff);
  assert(pixels[120 * LCD_WIDTH + 12] > pixels[0]);
  assert(pixels[120 * LCD_WIDTH + 12] < 0xffff);
  reset(); drawVignette(211, 178, 150); unchanged();
  drawVignette(178, 178, 150); unchanged();
  drawVignette(178, 211, 0); unchanged();
  canvas.framebuffer = nullptr; drawVignette(178, 211, 150); unchanged();
  reset(); drawVignette(178, 211, 256); guards();
  assert(pixels[0] == 0 && pixels[120 * LCD_WIDTH + 120] == 0xffff);
  puts("PASS drawVignette: crash arguments, canaries, corners, center and symmetry");
}
void pointNear(float p, float expectedX, float expectedY) {
  float x, y; framePoint(p, x, y);
  assert(std::hypot(x - expectedX, y - expectedY) < 0.002f);
}
void testFrame() {
  pointNear(0, 119.5f, 6); pointNear(0.25f, 233, 119.5f);
  pointNear(0.5f, 119.5f, 233); pointNear(0.75f, 6, 119.5f);
  pointNear(1, 119.5f, 6); pointNear(-1, 119.5f, 6); pointNear(2, 119.5f, 6);
  float oldX, oldY, distance = 0;
  framePoint(0, oldX, oldY);
  for (int i = 1; i <= 10000; ++i) {
    float x, y; framePoint(i / 10000.0f, x, y);
    assert(std::isfinite(x) && std::isfinite(y));
    assert(x >= 5.999f && x <= 233.001f && y >= 5.999f && y <= 233.001f);
    float step = std::hypot(x - oldX, y - oldY);
    assert(step > 0.07f && step < 0.12f);
    distance += step; oldX = x; oldY = y;
  }
  // Rounded 227x227 square, radius 14, measured from sampled production points.
  assert(std::abs(distance - (4 * (227 - 28) + 2 * PI * 14)) < 0.1f);
  puts("PASS framePoint: endpoints, quadrants, bounds and 10000 continuous segments");
}
void testRectAndCap() {
  reset(); capLine(-92, 333, 0); guards();
  assert(std::all_of(pixels, pixels + PIXELS, [](uint16_t c) { return c == 0; }));
  reset(); capLine(-1, 2, 0); guards();
  assert(pixels[0] == 0 && pixels[LCD_WIDTH] == 0xffff);
  reset(); capLine(239, 50, 0); guards();
  assert(pixels[238 * LCD_WIDTH] == 0xffff && pixels[PIXELS - 1] == 0);
  reset(); capLine(240, 3, 0); capLine(-20, 10, 0);
  capLine(0, 0, 0); capLine(0, -1, 0);
  capLine(INT_MAX, INT_MAX, 0); capLine(INT_MIN, INT_MAX, 0); unchanged();
  reset(); dimRect(-10, -10, 20, 20, 0, 0); guards();
  assert(pixels[0] == 0 && pixels[9 * LCD_WIDTH + 9] == 0);
  assert(pixels[10] == 0xffff && pixels[10 * LCD_WIDTH] == 0xffff);
  reset(); dimRect(-100, -100, 440, 440, 0, 0); guards();
  assert(std::all_of(pixels, pixels + PIXELS, [](uint16_t c) { return c == 0; }));
  reset(); dimRect(10, 10, 20, 20, 5, 0); guards();
  assert(pixels[10 * LCD_WIDTH + 10] == 0xffff && pixels[20 * LCD_WIDTH + 20] == 0);
  reset(); dimRect(240, 240, 10, 10, 0, 0);
  dimRect(-20, -20, 10, 10, 0, 0); dimRect(0, 0, 0, 10, 0, 0);
  dimRect(0, 0, 10, -1, 0, 0); unchanged();
  canvas.framebuffer = nullptr; dimRect(0, 0, 240, 240, 0, 0); unchanged();
  puts("PASS dimRect/capLine: clipping, corners, empty and outside rectangles/rows");
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  testRows(); testVignette(); testFrame(); testRectAndCap();
  puts("PASS BOX0 bounds regression (extracted production functions)");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=DEFAULT_NDK)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "artifacts/box0-test")
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    sketch = ROOT / "firmware/boards/box0/MotoNav/MotoNav.ino"
    source = sketch.read_text(encoding="utf-8")
    pins = sketch.with_name("pin_config.h").read_text(encoding="utf-8")
    defines = []
    for text, names in ((pins, ("LCD_WIDTH", "LCD_HEIGHT")),
                        (source, ("FRAME_INSET", "FRAME_RADIUS"))):
        for name in names:
            match = re.search(rf"^#define\s+{name}\s+\d+", text, re.MULTILINE)
            if not match:
                raise ValueError(f"Missing integer production constant: {name}")
            defines.append(match.group())
    definitions = []
    for name in ("blendF", "blend565", "framePoint", "parallelRows",
                 "drawVignette", "capLine", "dimRect"):
        line, definition = function(source, name)
        definitions.append(f'#line {line} "{sketch.as_posix()}"\n{definition}\n')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cpp, executable = output / "bounds-test.cpp", output / "bounds-test"
    cpp.write_text("// Generated from current BOX0 source; do not edit.\n" +
                   "\n".join(defines) + SUPPORT + "\n".join(definitions) +
                   '\n#line 1 "box0-bounds-assertions"\n' + TESTS, encoding="utf-8")
    print(f"Generated: {cpp}", flush=True)
    if args.generate_only:
        return
    prebuilt = args.ndk / "toolchains/llvm/prebuilt/windows-x86_64"
    command = [str(prebuilt / "bin/clang++.exe"),
               "--target=aarch64-linux-android26", f"--sysroot={prebuilt / 'sysroot'}",
               "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-UNDEBUG", "-static-libstdc++", str(cpp), "-o", str(executable)]
    print(subprocess.list2cmdline(command), flush=True)
    # Keep compiler temporaries in the same writable artifact directory.
    build_env = dict(os.environ, TMP=str(output), TEMP=str(output))
    subprocess.run(command, check=True, env=build_env)
    print(f"Built (not run): {executable}", flush=True)


if __name__ == "__main__":
    main()
