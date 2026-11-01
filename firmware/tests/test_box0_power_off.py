"""Build a regression executable from the real BOX0 power-off function.

Run this script locally, then run power-off-test and power-off-test --released
on ARM64 Android separately. This script never accesses a device or serial port.
"""

import argparse
import os
from pathlib import Path
import subprocess

from test_box0_bounds import DEFAULT_NDK, ROOT, function


SUPPORT = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
constexpr int C_BLACK = 0, HIGH = 1, LOW = 0;
constexpr int BTN_MIDDLE = 3, POWER_HOLD = 4, LCD_BL = 5;
constexpr int ESP_EXT1_WAKEUP_ANY_HIGH = 1;
enum Event { Clear, Flush, DisplayOff, BacklightOff, Read, Wait,
             HoldLow, Wakeup, Sleep };
std::vector<Event> events;
int heldReads = 3;
bool released = false, holdLow = false, wakeupReady = false;
struct Sleeping {};
void box0PowerOff();
struct MockSerial { void println(const char *) {} } Serial;
struct MockCanvas {
  void fillScreen(int color) { assert(color == C_BLACK); events.push_back(Clear); }
  void flush() { events.push_back(Flush); }
} canvas;
MockCanvas *gfx = &canvas;
struct MockPanel { void displayOff() { events.push_back(DisplayOff); } } screen;
MockPanel *panel = &screen;
struct BLEDevice {
  // Reject the historical teardown at compile time while BLE is still active.
  static void deinit(bool) = delete;
};
void ledcWrite(int pin, int value) {
  assert(pin == LCD_BL && value == 0);
  events.push_back(BacklightOff);
}
int digitalRead(int pin) {
  assert(pin == BTN_MIDDLE && !holdLow);
  events.push_back(Read);
  if (heldReads > 0) { --heldReads; return HIGH; }
  released = true;
  return LOW;
}
void delay(int milliseconds) {
  assert(milliseconds == 10 && !released && !holdLow);
  events.push_back(Wait);
  // A repeated shutdown request while waiting must do nothing.
  const auto before = events;
  box0PowerOff();
  assert(events == before);
}
void digitalWrite(int pin, int value) {
  assert(pin == POWER_HOLD && value == LOW && released);
  holdLow = true;
  events.push_back(HoldLow);
}
void esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int mode) {
  assert(holdLow && mask == (1ULL << BTN_MIDDLE));
  assert(mode == ESP_EXT1_WAKEUP_ANY_HIGH);
  wakeupReady = true;
  events.push_back(Wakeup);
}
void esp_deep_sleep_start() {
  assert(released && holdLow && wakeupReady);
  events.push_back(Sleep);
  throw Sleeping{}; // Simulate the hardware call never returning.
}
'''

TEST = r'''
int main(int argc, char **argv) {
  assert(argc == 1 || (argc == 2 && std::strcmp(argv[1], "--released") == 0));
  heldReads = argc == 2 ? 0 : 3;
  std::vector<Event> expected = {Clear, Flush, DisplayOff, BacklightOff};
  for (int i = 0; i < heldReads; ++i) {
    expected.push_back(Read);
    expected.push_back(Wait);
  }
  expected.insert(expected.end(), {Read, HoldLow, Wakeup, Sleep});
  bool slept = false;
  try { box0PowerOff(); } catch (const Sleeping &) { slept = true; }
  assert(slept && events == expected);
  // The same boot must not run the shutdown sequence a second time.
  box0PowerOff();
  assert(events == expected);
  std::puts("PASS: BLE survives; release -> hold low -> wakeup -> sleep; repeat ignored");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=DEFAULT_NDK)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "artifacts/box0-test")
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    sketch = ROOT / "firmware/boards/box0/MotoNav/MotoNav.ino"
    line, definition = function(sketch.read_text(encoding="utf-8"), "box0PowerOff")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cpp, executable = output / "power-off-test.cpp", output / "power-off-test"
    cpp.write_text(
        "// Generated from current BOX0 source; do not edit.\n" + SUPPORT
        + f'\n#line {line} "{sketch.as_posix()}"\n{definition}\n'
        + '\n#line 1 "box0-power-off-assertions"\n' + TEST,
        encoding="utf-8",
    )
    print(f"Generated: {cpp}", flush=True)
    if args.generate_only:
        return
    prebuilt = args.ndk / "toolchains/llvm/prebuilt/windows-x86_64"
    command = [str(prebuilt / "bin/clang++.exe"),
               "--target=aarch64-linux-android26", f"--sysroot={prebuilt / 'sysroot'}",
               "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-UNDEBUG", "-static-libstdc++", str(cpp), "-o", str(executable)]
    print(subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True,
                   env=dict(os.environ, TMP=str(output), TEMP=str(output)))
    print(f"Built (not run): {executable}", flush=True)


if __name__ == "__main__":
    main()
