#pragma once

// Test only: prepare_box0_probe.py embeds this after the sketch's definitions.
// Start with STATUS/CAPTURE on live phone data; scenes are explicit previews.
#include <esp_heap_caps.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t PROBE_PIXELS = (size_t)LCD_WIDTH * LCD_HEIGHT;
static constexpr uint16_t PROBE_GUARD = 0xA55A;
static uint16_t *probeStorage = nullptr;
static unsigned long probeGuardAt = 0;
static char probeCommand[64];
static size_t probeCommandLength = 0;
static bool probeCommandOverflow = false;

static void probeInit() {
  if (probeStorage) return;
  uint16_t *original = gfx->getFramebuffer();
  if (!original) {
    Serial.println("[GUARD] failed: no canvas");
    return;
  }
  uint16_t *storage = (uint16_t *)heap_caps_malloc(
      3 * PROBE_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!storage) {
    Serial.println("[GUARD] failed: PSRAM allocation");
    return;
  }
  for (size_t i = 0; i < PROBE_PIXELS; ++i) {
    storage[i] = PROBE_GUARD;
    storage[2 * PROBE_PIXELS + i] = PROBE_GUARD;
  }
  uint16_t *middle = storage + PROBE_PIXELS;
  memcpy(middle, original, PROBE_PIXELS * sizeof(uint16_t));
  gfx->setFramebuffer(middle);
  free(original);
  probeStorage = storage;  // Keep the allocation base; the canvas uses its middle.
  probeGuardAt = millis();
  Serial.println("[PROBE] guarded canvas installed; STATUS/CAPTURE ready");
}

static void probeCheckGuards() {
  bool front = probeStorage != nullptr;
  bool back = probeStorage != nullptr;
  if (probeStorage) {
    for (size_t i = 0; i < PROBE_PIXELS; ++i) {
      if (probeStorage[i] != PROBE_GUARD) front = false;
      if (probeStorage[2 * PROBE_PIXELS + i] != PROBE_GUARD) back = false;
    }
  }
  // Always check the heap, even if either sentinel region failed.
  bool heap = heap_caps_check_integrity_all(true);
  bool canvas = probeStorage && gfx->getFramebuffer() == probeStorage + PROBE_PIXELS;
  Serial.printf("[GUARD] %s front=%d back=%d heap=%d canvas=%d\n",
                front && back && heap && canvas ? "ok" : "failed",
                front, back, heap, canvas);
}

static void probeCapture() {
  const uint16_t *frame = gfx->getFramebuffer();
  if (!frame) {
    Serial.println("[PROBE] capture failed: no canvas");
    return;
  }
  static const char hex[] = "0123456789ABCDEF";
  char row[LCD_WIDTH * 4 + 1];
  row[LCD_WIDTH * 4] = '\n';
  Serial.setTxTimeoutMs(1000);
  Serial.printf("[FRAME] %d %d\n", LCD_WIDTH, LCD_HEIGHT);
  for (int y = 0; y < LCD_HEIGHT; ++y) {
    for (int x = 0; x < LCD_WIDTH; ++x) {
      uint16_t pixel = frame[(size_t)y * LCD_WIDTH + x];
      row[x * 4] = hex[(pixel >> 12) & 15];
      row[x * 4 + 1] = hex[(pixel >> 8) & 15];
      row[x * 4 + 2] = hex[(pixel >> 4) & 15];
      row[x * 4 + 3] = hex[pixel & 15];
    }
    Serial.printf("[ROW] %d ", y);
    Serial.write((const uint8_t *)row, sizeof(row));
  }
  Serial.print("[FRAME_END]\n");
  Serial.flush();
  Serial.setTxTimeoutMs(0);
}

// Rasterize short sample labels into the existing 4-bit text-image slots.
// The subsequent renderPage() replaces this temporary scratch drawing.
static void probeTextSlot(int slot, const char *text) {
  TextImg &image = imgs[slot];
  if (!image.show) return;
  const int width = min((int)strlen(text) * 12, LCD_WIDTH);
  const int height = 16;
  const int stride = (width + 1) / 2;
  gfx->fillRect(0, 0, width, height, C_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(0, 0);
  gfx->print(text);
  memset(image.show, 0, stride * height);
  const uint16_t *frame = gfx->getFramebuffer();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (frame[(size_t)y * LCD_WIDTH + x] != C_BLACK)
        image.show[y * stride + x / 2] |= (x & 1) ? 0x0F : 0xF0;
    }
  }
  image.showW = width;
  image.showH = height;
}

static void probeDispatch(const char *command) {
  if (strcmp(command, "CAPTURE") == 0) {
    probeCapture();
  } else if (strcmp(command, "STATUS") == 0) {
    Serial.printf("[STATUS] version=%s nav=%d map=%d page=%d uptime=%lu\n",
                  FW_VERSION, inNav, mapModeActive(), page, millis() / 1000);
  } else if (strcmp(command, "MAP") == 0) {
    // Resume whatever map the phone supplied; never synthesize map data.
    page = 0;
    mapOn = true;
    navDirty = true;
  } else if (strcmp(command, "PAIR") == 0) {
    uint32_t realCode = pairCode;
    bool realLocked = pairLocked;
    pairCode = 123456;
    pairLocked = false;
    drawPairScreen();
    pairCode = realCode;
    pairLocked = realLocked;
    printCentered("TEST CODE - NOT FOR PAIRING", CX, 190, 1, C_ORANGE);
  } else if (strcmp(command, "HOME") == 0) {
    page = 0;
    drawWaiting(bleConnected && bleAuthed ? WAIT_READY : WAIT_NOLINK);
  } else if (strcmp(command, "MUSIC") == 0 || strcmp(command, "WEATHER") == 0) {
    page = strcmp(command, "MUSIC") == 0 ? 2 : 1;
    pageAt = millis();
    renderPage(page);  // No showPageAnimated()/music requests or media actions.
  } else if (strcmp(command, "NAV") == 0) {
    page = 0;
    handleText("#m,0");
    handleText("#z,0");
    handleText("R,320,90");
    handleText("#v,58");
    handleText("#k,60,180");
    handleText("#j,0033F0");
    probeTextSlot(0, "PROBE ROAD");
    probeTextSlot(1, "TEST TURN RIGHT");
    renderPage(page);
  } else {
    Serial.println("[PROBE] unknown command");
  }
}

static void probeTick() {
  unsigned long now = millis();
  if (now - probeGuardAt >= 5000) {
    probeGuardAt = now;
    probeCheckGuards();
  }
  // Bound input work per loop; never wait for a newline or use readString().
  for (size_t n = 0; n < sizeof(probeCommand) && Serial.available() > 0; ++n) {
    int value = Serial.read();
    if (value < 0) break;
    char c = (char)value;
    if (c == '\r') continue;
    if (c == '\n') {
      if (probeCommandOverflow) Serial.println("[PROBE] command too long");
      else if (probeCommandLength) {
        probeCommand[probeCommandLength] = '\0';
        probeDispatch(probeCommand);
      }
      probeCommandLength = 0;
      probeCommandOverflow = false;
    } else if (probeCommandLength < sizeof(probeCommand) - 1) {
      probeCommand[probeCommandLength++] = c;
    } else {
      probeCommandOverflow = true;  // Discard the whole overlong command.
    }
  }
}
