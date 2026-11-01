#define FW_VERSION "v1.6.0-CYD"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <SPI.h>
#include "pin_config.h"

static constexpr char SERVICE_UUID[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr char RX_UUID[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr char TX_UUID[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static constexpr uint16_t BG = 0x0841;
static constexpr uint16_t PANEL = 0x10A3;
static constexpr uint16_t WHITE = 0xFFFF;
static constexpr uint16_t MUTED = 0x9CF3;
static constexpr uint16_t GREEN = 0x3E68;
static constexpr uint16_t CYAN = 0x3DDF;
static constexpr uint16_t ORANGE = 0xFD20;
static constexpr uint16_t RED = 0xF986;
static constexpr uint16_t MAP_SIZE = 320;
static constexpr uint32_t MAP_PIXELS = MAP_SIZE * MAP_SIZE;
static constexpr uint32_t BULK_CAPACITY = 80000;
static constexpr uint16_t BULK_PARTS = 256;
static constexpr uint8_t RX_SLOTS = 8;
static constexpr uint16_t RX_PACKET_SIZE = 520;
static constexpr uint8_t TRAFFIC_MAX = 40;
static constexpr uint8_t CAMERA_MAX = 10;

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO, HSPI, true);
Arduino_ST7789 *lcd = new Arduino_ST7789(lcdBus, LCD_RESET, 1, true, 240, 320);
SPIClass touchSPI(VSPI);
BLECharacteristic *txChar = nullptr;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t rxPackets[RX_SLOTS][RX_PACKET_SIZE];
uint16_t rxLengths[RX_SLOTS] = {};
volatile uint8_t rxHead = 0, rxTail = 0;
volatile bool bleConnected = false;
uint8_t *bulkBuffer = nullptr;
uint8_t bulkMask[BULK_PARTS / 8] = {};
uint8_t mapPixels[MAP_PIXELS / 2] = {};
uint16_t mapPalette[16] = {BG, 0x73AE, 0xFFE0, 0x2E9F, 0xC618, 0x9CF3, 0x3E68, 0x3DDF};
int32_t mapCenterX = 0, mapCenterY = 0;
uint16_t mapFrame = 0;
float mapMetersPerPixel = 7.5f;
bool mapValid = false;
uint8_t mapTheme = 1;
bool nightOn = false, cleanOn = false, curveOn = false, progressOn = true;
uint8_t trailMode = 1;
uint8_t nightModeChoice = 0;
bool mapEnabled = true;
uint8_t screenBrightness = 153;
int32_t positionX = 0, positionY = 0;
uint16_t positionFrame = 0;
int positionHeading = 0;
bool havePosition = false;
int16_t routeX[5000], routeY[5000];
uint16_t routeCount = 0, routeFrame = 0;
static constexpr uint8_t ROUTE_LIGHTS_MAX = 40;
int32_t routeLightX[ROUTE_LIGHTS_MAX] = {}, routeLightY[ROUTE_LIGHTS_MAX] = {};
uint32_t routeLightAlong[ROUTE_LIGHTS_MAX] = {};
uint8_t routeLightCount = 0;
int16_t *ghostPoints = nullptr;
int32_t ghostOriginX = 0, ghostOriginY = 0;
uint16_t ghostCount = 0, ghostFrame = 0;
int32_t trafficFrom[TRAFFIC_MAX] = {}, trafficTo[TRAFFIC_MAX] = {};
uint8_t trafficStatus[TRAFFIC_MAX] = {}, trafficCount = 0;
struct CameraPoint { int32_t x = 0, y = 0; uint16_t speed = 0; uint8_t type = 0; };
CameraPoint cameraPoints[CAMERA_MAX] = {};
uint16_t cameraFrame = 0;
uint8_t cameraPointCount = 0;
int32_t tripOriginX = 0, tripOriginY = 0;
int32_t routeOriginX = 0, routeOriginY = 0;
struct BulkState { uint8_t type = 0, id = 0; uint16_t parts = 0, got = 0; uint32_t total = 0; bool active = false; } bulk;

enum Page : uint8_t { MAP_PAGE, ROUTE_PAGE, INFO_PAGE, MUSIC_PAGE, SETTINGS_PAGE };
Page page = MAP_PAGE;
char turnCode = 'S';
int distanceM = 0;
int speedKmh = 0;
int speedLimit = 0;
int remainM = 0;
int eta = 0;
bool navigating = false;
bool gpsWeak = false;
bool cruiseOn = false;
bool hasCamera = false;
bool weatherHave = false;
bool callOn = false;
bool callAnswerSent = false;
bool callInfoDirty = true;
uint8_t laneCount = 0;
uint8_t laneBg[8] = {}, laneFg[8] = {};
int cameraLimit = 0, cameraDistance = 0, lightDistance = -1;
int tripLights = 0, tripMeters = -1, tripMinutes = -1, tripMaxKmh = -1;
int weatherIcon = 2, weatherTemp = 0, weatherAqi = -1, weatherWarningCount = 0;
int weatherWarningLevel = 0, weatherRainCount = 0;
uint8_t weatherRain[12] = {};
bool weatherHourly = true;
uint8_t weatherForecastCount = 0, weatherForecastHour[6] = {}, weatherForecastIcon[6] = {};
int8_t weatherForecastHigh[6] = {}, weatherForecastLow[6] = {};
uint8_t weatherView = 0;
char weatherTime[8] = "";
char callNumber[24] = "";
uint32_t callSeenAt = 0, callHoldAt = 0;
uint32_t lastMapDrawAt = 0;
bool mapRenderPending = false;
uint32_t rerouteAt = 0, fasterAt = 0;
int fasterMinutes = 0;
int32_t tripProgressDone = 0, tripProgressRemaining = -1;
uint16_t leadMs = 300;
uint32_t positionAt = 0;
int positionSpeedKmh = 0;
bool musicHave = false, musicPlaying = false;
uint8_t musicActions = 0, musicAck = 0;
struct TextImage { uint8_t *data = nullptr; uint16_t width = 0, height = 0, capacity = 0; };
TextImage appImages[12];
uint8_t *imageRx = nullptr;
uint16_t imageRxCapacity = 0;
uint32_t imageMask = 0;
uint16_t imageWidth = 0, imageHeight = 0, imageParts = 0;
uint8_t imageId = 0, imageSlot = 0;
bool imageActive = false;
static void drawTextImage(const TextImage &image, int top, int maxWidth = 280);
uint16_t rawXMin = 250, rawXMax = 3850, rawYMin = 250, rawYMax = 3850;
int16_t touchDownX = 0, touchDownY = 0;
bool touchWasDown = false;
bool touchSwipeHandled = false;
int8_t reverseTouchX = 0;
int8_t reverseTouchY = 0;
int8_t touchSwapXY = 0;
bool calibrating = false;
bool settingsDirty = false;
uint8_t calibrationStep = 0;
int16_t calibX[4], calibY[4];
Page renderedPage = SETTINGS_PAGE;
bool renderedCalibrating = false;
uint8_t renderedCalibrationStep = 0;
uint16_t mapRow[MAP_SIZE];

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { bleConnected = true; }
  void onDisconnect(BLEServer *) override {
    bleConnected = false;
    BLEDevice::startAdvertising();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    size_t length = characteristic->getLength();
    if (!length || length > RX_PACKET_SIZE) return;
    portENTER_CRITICAL(&rxMux);
    uint8_t next = (rxHead + 1) % RX_SLOTS;
    if (next == rxTail) rxTail = (rxTail + 1) % RX_SLOTS;
    memcpy(rxPackets[rxHead], characteristic->getData(), length);
    rxLengths[rxHead] = length;
    rxHead = next;
    portEXIT_CRITICAL(&rxMux);
  }
};

static int clampInt(int value, int low, int high) {
  return value < low ? low : value > high ? high : value;
}

static uint8_t hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return 0;
}

static void text(const char *value, int x, int y, uint8_t size, uint16_t ink) {
  lcd->setTextSize(size);
  lcd->setTextColor(ink);
  lcd->setCursor(x, y);
  lcd->print(value);
}

static void notifyTouch(int x, int y) {
  if (!txChar || !bleConnected) return;
  char message[36];
  snprintf(message, sizeof(message), "T,P,%d,%d", x, y);
  txChar->setValue((uint8_t *)message, strlen(message));
  txChar->notify();
}

static void sendMusicAction(char action) {
  if (!txChar || !bleConnected) return;
  char message[] = "T,M,X";
  message[4] = action;
  txChar->setValue((uint8_t *)message, sizeof(message) - 1);
  txChar->notify();
}

static void sendSettingEvent(uint8_t setting, uint8_t value) {
  if (!txChar || !bleConnected) return;
  char message[20];
  snprintf(message, sizeof(message), "T,S,%u,%u", setting, value);
  txChar->setValue((uint8_t *)message, strlen(message));
  txChar->notify();
}

static void sendPhoneEvent(const char *event) {
  if (!txChar || !bleConnected) return;
  txChar->setValue((uint8_t *)event, strlen(event));
  txChar->notify();
}

static void clearGhostRoute() {
  free(ghostPoints);
  ghostPoints = nullptr;
  ghostCount = 0;
}

static void parseMessage(char *message) {
  if (!strcmp(message, "C")) {
    navigating = false;
    turnCode = 'S';
    distanceM = 0;
    routeCount = 0;
    routeLightCount = 0;
    clearGhostRoute();
    remainM = 0;
    tripProgressDone = 0;
    tripProgressRemaining = -1;
    for (uint8_t i = 0; i < 2; ++i) {
      free(appImages[i].data);
      appImages[i].data = nullptr;
      appImages[i].width = appImages[i].height = appImages[i].capacity = 0;
    }
    return;
  }
  if (!strcmp(message, "#GB?")) { sendPhoneEvent("CAP,BLD,1"); return; }
  if (!strncmp(message, "#BOOT,", 6)) { sendPhoneEvent("BOOT,0"); return; }
  if (!strncmp(message, "#p,", 3)) {
    int frame = 0, heading = 0, kmh = 0;
    long xDm = 0, yDm = 0;
    if (sscanf(message, "#p,%d,%ld,%ld,%d,%d", &frame, &xDm, &yDm, &heading, &kmh) == 5) {
      positionFrame = (uint16_t)frame;
      positionX = xDm / 10;
      positionY = yDm / 10;
      positionHeading = (heading % 360 + 360) % 360;
      speedKmh = clampInt(kmh, 0, 300);
      positionSpeedKmh = speedKmh;
      positionAt = millis();
      havePosition = true;
      if (mapTheme == 11) mapFrame = positionFrame;
    }
    return;
  }
  if (!strcmp(message, "#k")) { hasCamera = false; cameraLimit = cameraDistance = 0; return; }
  if (!strcmp(message, "#g")) { lightDistance = -1; return; }
  if (!strcmp(message, "#j")) { laneCount = 0; return; }
  if (!strcmp(message, "#u")) { trafficCount = 0; return; }
  if (!strcmp(message, "#c")) { cameraPointCount = 0; return; }
  if (!strcmp(message, "#WXRESET")) {
    weatherHave = false;
    weatherAqi = -1;
    weatherWarningCount = weatherRainCount = 0;
    weatherForecastCount = 0;
    return;
  }
  if (!strncmp(message, "#j,", 3)) {
    laneCount = 0;
    size_t chars = strlen(message + 3);
    uint8_t count = min((size_t)8, chars / 2);
    for (uint8_t i = 0; i < count; ++i) {
      laneBg[i] = hexValue(message[3 + 2 * i]);
      laneFg[i] = hexValue(message[4 + 2 * i]);
    }
    laneCount = count;
    return;
  }
  if (!strncmp(message, "#W,", 3)) {
    char *save = nullptr;
    strtok_r(message + 1, ",", &save);
    char *icon = strtok_r(nullptr, ",", &save);
    char *temp = strtok_r(nullptr, ",", &save);
    char *time = strtok_r(nullptr, ",", &save);
    char *kind = strtok_r(nullptr, ",", &save);
    char *count = strtok_r(nullptr, ",", &save);
    if (!icon || !temp) return;
    weatherIcon = clampInt(atoi(icon), 0, 11);
    weatherTemp = clampInt(atoi(temp), -80, 80);
    snprintf(weatherTime, sizeof(weatherTime), "%s", time ? time : "");
    weatherHourly = kind && kind[0] == 'H';
    weatherForecastCount = kind && count ? min((uint8_t)atoi(count), (uint8_t)(weatherHourly ? 6 : 4)) : 0;
    for (uint8_t i = 0; i < weatherForecastCount; ++i) {
      char *a = strtok_r(nullptr, ",", &save);
      char *b = strtok_r(nullptr, ",", &save);
      char *c = strtok_r(nullptr, ",", &save);
      char *d = strtok_r(nullptr, ",", &save);
      if (!a || !b || !c || !d) { weatherForecastCount = i; break; }
      weatherForecastHour[i] = clampInt(atoi(a), 0, 99);
      weatherForecastIcon[i] = clampInt(atoi(b), 0, 11);
      weatherForecastHigh[i] = clampInt(atoi(c), -80, 80);
      weatherForecastLow[i] = clampInt(atoi(d), 0, 100);
    }
    weatherHave = true;
    return;
  }
  if (!strncmp(message, "#WX,", 4)) {
    char *save = nullptr;
    strtok_r(message + 1, ",", &save);
    int values[5] = {};
    for (int i = 0; i < 5; ++i) {
      char *value = strtok_r(nullptr, ",", &save);
      if (!value) return;
      values[i] = atoi(value);
    }
    weatherAqi = clampInt(values[0], 0, 1000);
    weatherWarningCount = clampInt(values[1], 0, 99);
    weatherWarningLevel = clampInt(values[2], 0, 9);
    weatherRainCount = clampInt(values[3], 0, 12);
    strtok_r(nullptr, ",", &save);
    for (int i = 0; i < weatherRainCount; ++i) {
      char *bar = strtok_r(nullptr, ",", &save);
      if (!bar) { weatherRainCount = i; break; }
      weatherRain[i] = clampInt(atoi(bar), 0, 9);
    }
    return;
  }
  if (!strncmp(message, "#i,", 3)) {
    int on = 0;
    char number[sizeof(callNumber)] = "";
    if (sscanf(message, "#i,%d,%23[^,]", &on, number) >= 1) {
      bool wasOn = callOn;
      callOn = on != 0;
      callInfoDirty = true;
      if (callOn) {
        if (!wasOn) { callAnswerSent = false; callHoldAt = 0; }
        callSeenAt = millis();
        snprintf(callNumber, sizeof(callNumber), "%s", number);
      } else {
        callNumber[0] = '\0';
        free(appImages[2].data);
        appImages[2].data = nullptr;
        appImages[2].width = appImages[2].height = appImages[2].capacity = 0;
      }
    }
    return;
  }
  if (!strncmp(message, "#O", 2)) {
    long meters = -1;
    int minutes = -1, maxKmh = -1;
    if (sscanf(message + 2, "%ld/%d/%d", &meters, &minutes, &maxKmh) >= 2) {
      tripMeters = clampInt(meters, 0, 2000000);
      tripMinutes = clampInt(minutes, 0, 100000);
      tripMaxKmh = clampInt(maxKmh, 0, 300);
    }
    return;
  }
  if (!strncmp(message, "#L", 2)) { leadMs = clampInt(atoi(message + 2), 0, 800); return; }
  if (!strncmp(message, "#m,", 3)) { mapEnabled = atoi(message + 3) != 0; return; }
  if (!strncmp(message, "#b,", 3)) { screenBrightness = clampInt(atoi(message + 3), 5, 255); analogWrite(LCD_BL, screenBrightness); return; }
  if (!strncmp(message, "#t,", 3)) { mapTheme = clampInt(atoi(message + 3), 0, 11); if (mapTheme == 11 && havePosition) mapFrame = positionFrame; settingsDirty = true; return; }
  if (!strncmp(message, "#N,", 3)) { nightModeChoice = clampInt(atoi(message + 3), 0, 2); settingsDirty = true; return; }
  if (!strncmp(message, "#n,", 3)) { nightOn = atoi(message + 3) != 0; settingsDirty = true; return; }
  if (!strncmp(message, "#r,", 3)) { int mode = atoi(message + 3); if (mode == 1 || mode == 2) trailMode = mode; settingsDirty = true; return; }
  if (!strncmp(message, "#w,", 3)) { cleanOn = atoi(message + 3) != 0; settingsDirty = true; return; }
  if (!strncmp(message, "#Q,", 3)) { curveOn = atoi(message + 3) != 0; settingsDirty = true; return; }
  if (!strncmp(message, "#M", 2)) { progressOn = atoi(message + 2) != 0; settingsDirty = true; return; }
  if (!strcmp(message, "#o")) { clearGhostRoute(); return; }
  if (!strncmp(message, "#y,", 3)) {
    rerouteAt = atoi(message + 3) ? max(1UL, millis()) : 0;
    if (rerouteAt) fasterAt = 0;
    return;
  }
  if (!strcmp(message, "#h")) { fasterAt = 0; return; }
  if (!strncmp(message, "#h,", 3)) {
    fasterMinutes = clampInt(atoi(message + 3), 0, 999);
    fasterAt = max(1UL, millis());
    rerouteAt = 0;
    return;
  }
  if (!strncmp(message, "#u,", 3)) {
    trafficCount = 0;
    char *save = nullptr;
    char *token = strtok_r(message + 3, ",", &save);
    while (token && trafficCount < TRAFFIC_MAX) {
      int32_t from = atoi(token);
      char *toToken = strtok_r(nullptr, ",", &save);
      char *statusToken = strtok_r(nullptr, ",", &save);
      if (!toToken || !statusToken) break;
      int32_t to = atoi(toToken);
      int status = atoi(statusToken);
      if (to > from && status >= 2 && status <= 4) {
        trafficFrom[trafficCount] = from;
        trafficTo[trafficCount] = to;
        trafficStatus[trafficCount++] = status;
      }
      token = strtok_r(nullptr, ",", &save);
    }
    return;
  }
  if (!strncmp(message, "#c,", 3)) {
    char *save = nullptr;
    char *token = strtok_r(message + 3, ",", &save);
    if (!token) return;
    cameraFrame = (uint16_t)atoi(token);
    cameraPointCount = 0;
    while (cameraPointCount < CAMERA_MAX) {
      char *x = strtok_r(nullptr, ",", &save);
      char *y = strtok_r(nullptr, ",", &save);
      char *speed = strtok_r(nullptr, ",", &save);
      char *type = strtok_r(nullptr, ",", &save);
      if (!x || !y || !speed || !type) break;
      CameraPoint &point = cameraPoints[cameraPointCount++];
      point.x = atol(x); point.y = atol(y);
      point.speed = clampInt(atoi(speed), 0, 300);
      point.type = clampInt(atoi(type), 0, 31);
    }
    return;
  }
  if (!strncmp(message, "#Z,", 3)) {
    int have = 0, playing = 0, actions = 0;
    if (sscanf(message, "#Z,%d,%d,%d", &have, &playing, &actions) == 3) {
      musicHave = have != 0;
      musicPlaying = playing != 0;
      musicActions = actions & 7;
    }
    return;
  }
  if (!strncmp(message, "#ZACK,", 6)) {
    musicAck = clampInt(atoi(message + 6), 0, 5);
    return;
  }
  if (message[0] == '#') {
    char *save = nullptr;
    char *key = strtok_r(message + 1, ",", &save);
    char *value = strtok_r(nullptr, ",", &save);
    if (!key || !value) return;
    int number = atoi(value);
    if (!strcmp(key, "v")) speedKmh = clampInt(number, 0, 300);
    else if (!strcmp(key, "e")) {
      number = clampInt(number, 0, 999999);
      if (remainM > number && remainM - number < 5000) tripProgressDone += remainM - number;
      remainM = number;
      tripProgressRemaining = remainM;
      char *arrival = strtok_r(nullptr, ",", &save);
      if (arrival) eta = atoi(arrival);
      char *lights = strtok_r(nullptr, ",", &save);
      if (lights) tripLights = clampInt(atoi(lights), 0, 999);
    } else if (!strcmp(key, "k")) {
      speedLimit = clampInt(number, 0, 300);
      char *distance = strtok_r(nullptr, ",", &save);
      hasCamera = distance != nullptr;
      cameraLimit = speedLimit;
      cameraDistance = distance ? clampInt(atoi(distance), 0, 99999) : 0;
    } else if (!strcmp(key, "g")) lightDistance = clampInt(number, 0, 99999);
    else if (!strcmp(key, "x")) gpsWeak = number != 0;
    else if (!strcmp(key, "z")) cruiseOn = number != 0;
    return;
  }
  if (!strcmp(message, "A")) {
    navigating = false;
    return;
  }
  char *comma = strchr(message, ',');
  if (comma) {
    *comma++ = '\0';
    char *end = strchr(comma, ',');
    if (end) *end = '\0';
    if (strlen(message) == 1 && strchr("RLSU", message[0])) {
      turnCode = message[0];
      distanceM = clampInt(atoi(comma), 0, 99999);
      navigating = true;
    }
  }
}

static void drawArrow(int cx, int cy, char direction, uint16_t ink) {
  if (direction == 'L') {
    lcd->fillTriangle(cx - 23, cy - 15, cx - 23, cy + 15, cx - 3, cy, ink);
    lcd->fillRoundRect(cx - 5, cy - 6, 33, 12, 5, ink);
    lcd->fillRoundRect(cx + 15, cy - 6, 12, 32, 5, ink);
  } else if (direction == 'R') {
    lcd->fillTriangle(cx + 23, cy - 15, cx + 23, cy + 15, cx + 3, cy, ink);
    lcd->fillRoundRect(cx - 28, cy - 6, 33, 12, 5, ink);
    lcd->fillRoundRect(cx - 28, cy - 6, 12, 32, 5, ink);
  } else {
    lcd->fillRoundRect(cx - 6, cy - 31, 12, 48, 5, ink);
    lcd->fillTriangle(cx, cy - 42, cx - 19, cy - 20, cx + 19, cy - 20, ink);
    if (direction == 'U') {
      lcd->fillRoundRect(cx + 8, cy + 1, 23, 10, 4, ink);
      lcd->fillTriangle(cx + 34, cy + 6, cx + 19, cy - 4, cx + 19, cy + 16, ink);
    }
  }
}

static uint16_t readU16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static int16_t readI16(const uint8_t *p) { return (int16_t)readU16(p); }
static int32_t readI32(const uint8_t *p) {
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static bool handleImage(const uint8_t *data, size_t length) {
  if (!data || length < 10 || data[1] > 11) return false;
  uint8_t slot = data[1], id = data[2], part = data[3], parts = data[4];
  uint16_t width = readU16(data + 5), height = data[7], offset = readU16(data + 8);
  TextImage &target = appImages[slot];
  if (!width || !height) {
    free(target.data);
    target.data = nullptr; target.width = target.height = target.capacity = 0;
    if (slot == 2 && callOn) callInfoDirty = true;
    return true;
  }
  if (!parts || parts > 32 || part >= parts || width > 360 || height > 60) return false;
  uint32_t total = ((uint32_t)width + 1) / 2 * height;
  if (!total || total > 8000) return false;
  if (!imageActive || imageId != id || imageSlot != slot) {
    free(imageRx);
    imageRx = (uint8_t *)malloc(total);
    if (!imageRx) { imageRxCapacity = 0; imageActive = false; return false; }
    imageRxCapacity = total;
    imageId = id; imageSlot = slot; imageWidth = width; imageHeight = height;
    imageParts = parts; imageMask = 0; imageActive = true;
  }
  if (imageWidth != width || imageHeight != height || imageParts != parts || total > imageRxCapacity) return false;
  if (offset > total) return false;
  size_t bytes = min((size_t)(length - 10), (size_t)(total - offset));
  if ((uint32_t)offset + bytes > total) return false;
  memcpy(imageRx + offset, data + 10, bytes);
  imageMask |= 1UL << part;
  uint32_t complete = parts == 32 ? 0xffffffffUL : ((1UL << parts) - 1);
  if ((imageMask & complete) != complete) return false;
  if (target.capacity < total) {
    uint8_t *resized = (uint8_t *)realloc(target.data, total);
    if (!resized) return false;
    target.data = resized;
    target.capacity = total;
  }
  memcpy(target.data, imageRx, total);
  target.width = width; target.height = height;
  if (slot == 2 && callOn) callInfoDirty = true;
  free(imageRx);
  imageRx = nullptr; imageRxCapacity = 0;
  imageActive = false;
  return true;
}

static void setMapPixel(int x, int y, uint8_t color) {
  if ((uint32_t)x >= MAP_SIZE || (uint32_t)y >= MAP_SIZE) return;
  uint32_t index = (uint32_t)y * MAP_SIZE + x;
  uint8_t &packed = mapPixels[index >> 1];
  if (index & 1) packed = (packed & 0x0f) | ((color & 15) << 4);
  else packed = (packed & 0xf0) | (color & 15);
}

static void drawMapLine(int x0, int y0, int x1, int y1, uint8_t color, uint8_t thickness = 1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    for (int oy = -(int)thickness / 2; oy <= (int)thickness / 2; ++oy)
      for (int ox = -(int)thickness / 2; ox <= (int)thickness / 2; ++ox)
        setMapPixel(x0 + ox, y0 + oy, color);
    if (x0 == x1 && y0 == y1) break;
    int twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

static bool parseVectorMap(const uint8_t *data, uint32_t length) {
  if (!data || length < 12) return false;
  uint16_t frame = readU16(data), features = readU16(data + 10);
  int32_t centerX = readI32(data + 2), centerY = readI32(data + 6);
  memset(mapPixels, 0, sizeof(mapPixels));
  mapPalette[0] = 0x0841; mapPalette[1] = 0xFFE0; mapPalette[2] = 0xFBE0;
  mapPalette[3] = 0xFFFF; mapPalette[4] = 0xAD55; mapPalette[5] = 0xFD20;
  mapPalette[6] = 0x3E68; mapPalette[7] = 0x3DDF;
  uint32_t offset = 12;
  for (uint16_t i = 0; i < features; ++i) {
    if (offset + 3 > length) return false;
    uint8_t cls = data[offset];
    uint16_t points = readU16(data + offset + 1);
    offset += 3;
    if (points < 2 || offset + (uint32_t)points * 4 > length) return false;
    uint8_t category = cls & 0x7f;
    bool polygon = cls & 0x80;
    uint8_t color = category == 10 ? 6 : category == 11 ? 7 : category <= 5 ? category : 4;
    uint8_t width = category <= 2 ? 3 : category == 3 ? 2 : 1;
    int previousX = 0, previousY = 0;
    for (uint16_t p = 0; p < points; ++p) {
      int16_t x = readI16(data + offset + 4 * p);
      int16_t y = readI16(data + offset + 4 * p + 2);
      int pixelX = (int)lroundf((x + 1200) / 7.5f);
      int pixelY = (int)lroundf((1200 - y) / 7.5f);
      if (p) drawMapLine(previousX, previousY, pixelX, pixelY, color, polygon ? 1 : width);
      previousX = pixelX; previousY = pixelY;
    }
    if (polygon) {
      int16_t x = readI16(data + offset), y = readI16(data + offset + 2);
      drawMapLine(previousX, previousY, (int)lroundf((x + 1200) / 7.5f), (int)lroundf((1200 - y) / 7.5f), color);
    }
    offset += (uint32_t)points * 4;
  }
  if (offset != length) return false;
  mapFrame = frame; mapCenterX = centerX; mapCenterY = centerY; mapMetersPerPixel = 7.5f;
  mapValid = true;
  return true;
}

static bool parseRasterMap(const uint8_t *data, uint32_t length) {
  if (!data || length < 16) return false;
  uint16_t frame = readU16(data), width = readU16(data + 10), height = readU16(data + 12);
  uint16_t flags = readU16(data + 14), mm = flags & 0x3fff;
  bool palette = flags & 0x8000;
  bool buildings = flags & 0x4000;
  if (!width || !height || !mm || width > 1300 || height > 1300) return false;
  uint32_t pixels = (uint32_t)width * height;
  uint32_t offset = palette ? 48 : 16;
  if (length < offset) return false;
  memset(mapPixels, 0, sizeof(mapPixels));
  if (palette) for (int i = 0; i < 16; ++i) mapPalette[i] = readU16(data + 16 + 2 * i);
  else {
    mapPalette[0] = 0x0841; mapPalette[1] = 0xAD55; mapPalette[2] = 0x3E68; mapPalette[3] = 0x3DDF;
    mapPalette[4] = 0x6B4D; mapPalette[5] = 0x946F; mapPalette[6] = 0x3E68; mapPalette[7] = 0x3DDF;
  }
  uint32_t decoded = 0;
  while (decoded < pixels) {
    if (offset >= length) return false;
    uint8_t token = data[offset++], color = token & 15;
    uint32_t run = (token >> 4) + 1;
    if ((token >> 4) == 15) {
      if (offset >= length) return false;
      run = 16 + data[offset++];
    }
    if ((!palette && color > (buildings ? 7 : 3)) || run > pixels - decoded) return false;
    if (color) for (uint32_t k = decoded; k < decoded + run; ++k) {
      int x = (k % width) * MAP_SIZE / width;
      int y = (k / width) * MAP_SIZE / height;
      setMapPixel(x, y, color);
    }
    decoded += run;
  }
  if (offset != length) return false;
  int32_t x0 = readI32(data + 2), y0 = readI32(data + 6);
  float mpp = mm / 1000.0f;
  mapFrame = frame;
  mapCenterX = x0 + (int32_t)lroundf(width * mpp * 0.5f);
  mapCenterY = y0 - (int32_t)lroundf(height * mpp * 0.5f);
  mapMetersPerPixel = width >= height ? width * mpp / MAP_SIZE : height * mpp / MAP_SIZE;
  mapValid = true;
  return true;
}

static bool parseRoute(const uint8_t *data, uint32_t length, bool ghost = false) {
  if (!data || length < 12 || readU16(data) != positionFrame) return false;
  uint16_t count = readU16(data + 10);
  if (count < 2 || count > 5000 || 12u + (uint32_t)(count - 1) * 4u > length) return false;
  int32_t x = readI32(data + 2), y = readI32(data + 6);
  int32_t originX = x, originY = y;
  int16_t *newGhost = ghost ? (int16_t *)malloc((size_t)count * 2 * sizeof(int16_t)) : nullptr;
  if (ghost && !newGhost) return false;
  for (uint16_t i = 0; i < count; ++i) {
    if (i) { x += readI16(data + 12 + 4 * (i - 1)); y += readI16(data + 14 + 4 * (i - 1)); }
    if (ghost) {
      int32_t dx = x - originX, dy = y - originY;
      if (dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767) { free(newGhost); return false; }
      newGhost[2 * i] = dx;
      newGhost[2 * i + 1] = dy;
    }
    else {
      int32_t dx = x - originX, dy = y - originY;
      if (dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767) return false;
      routeX[i] = dx; routeY[i] = dy;
    }
  }
  if (!ghost) {
    routeOriginX = originX; routeOriginY = originY;
    routeCount = count; routeFrame = readU16(data);
    routeLightCount = 0;
    uint32_t offset = 12u + (uint32_t)(count - 1) * 4u;
    if (offset < length) {
      if (offset + 2 > length) return false;
      uint16_t lights = readU16(data + offset);
      offset += 2;
      if (offset + (uint32_t)lights * 11u > length) return false;
      for (uint16_t i = 0; i < lights; ++i, offset += 11) {
        int32_t lx = readI32(data + offset), ly = readI32(data + offset + 4);
        uint16_t segment = min(readU16(data + offset + 8), (uint16_t)(count - 2));
        uint8_t fraction = data[offset + 10];
        float along = 0;
        for (uint16_t k = 1; k <= segment; ++k)
          along += hypotf((float)(routeX[k] - routeX[k - 1]), (float)(routeY[k] - routeY[k - 1]));
        along += hypotf((float)(routeX[segment + 1] - routeX[segment]), (float)(routeY[segment + 1] - routeY[segment])) * fraction / 255.0f;
        if (routeLightCount < ROUTE_LIGHTS_MAX) {
          uint8_t at = routeLightCount++;
          routeLightX[at] = lx; routeLightY[at] = ly; routeLightAlong[at] = max(0.0f, along);
        }
      }
      if (offset != length) return false;
    }
  } else {
    free(ghostPoints);
    ghostPoints = newGhost;
    ghostOriginX = originX; ghostOriginY = originY;
    ghostCount = count; ghostFrame = readU16(data);
  }
  if (mapTheme == 11) mapFrame = readU16(data);
  return true;
}

static void sendBulkAck(uint8_t type, uint8_t id) {
  if (!txChar || !bleConnected) return;
  char ack[20];
  snprintf(ack, sizeof(ack), "K,%u,%u", type, id);
  txChar->setValue((uint8_t *)ack, strlen(ack));
  txChar->notify();
}

static bool handleBulk(const uint8_t *data, size_t length) {
  if (length < 15) return false;
  uint8_t type = data[1], id = data[2];
  uint16_t parts = readU16(data + 3), seq = readU16(data + 5);
  uint32_t total = (uint32_t)readI32(data + 7), offset = (uint32_t)readI32(data + 11);
  if (type < 1 || type > 4 || !parts || parts > BULK_PARTS || seq >= parts || !total || total > BULK_CAPACITY || offset >= total) return false;
  if (!bulkBuffer) bulkBuffer = (uint8_t *)malloc(BULK_CAPACITY);
  if (!bulkBuffer) return false;
  if (!bulk.active || bulk.type != type || bulk.id != id || bulk.parts != parts || bulk.total != total) {
    bulk = {type, id, parts, 0, total, true};
    memset(bulkMask, 0, sizeof(bulkMask));
  }
  uint32_t bytes = min((uint32_t)(length - 15), total - offset);
  if (!(bulkMask[seq >> 3] & (1 << (seq & 7)))) {
    memcpy(bulkBuffer + offset, data + 15, bytes);
    bulkMask[seq >> 3] |= (1 << (seq & 7));
    ++bulk.got;
  }
  if (bulk.got != bulk.parts) return false;
  bool ok = type == 1 ? parseVectorMap(bulkBuffer, bulk.total)
           : type == 2 ? parseRoute(bulkBuffer, bulk.total)
           : type == 3 ? parseRasterMap(bulkBuffer, bulk.total)
           : parseRoute(bulkBuffer, bulk.total, true);
  if (ok) sendBulkAck(type, id);
  bulk.active = false;
  free(bulkBuffer);
  bulkBuffer = nullptr;
  return ok;
}

static uint16_t themedMapColor(uint8_t color) {
  static const uint16_t day[12][8] = {
    {0xFFFF,0xC618,0x8410,0xFFFF,0xAD55,0xF81F,0x07E0,0x001F},
    {0x0841,0x73AE,0xFFE0,0xFBE0,0xC618,0xF81F,0x3E68,0x3DDF},
    {0x0000,0x4208,0x7BEF,0xFFFF,0xBDF7,0x801F,0x39E7,0x001F},
    {0x0000,0x03EF,0x07FF,0x5FFF,0x39E7,0xF81F,0x07E0,0x001F},
    {0xE71C,0xD69A,0xC618,0xFFFF,0xA514,0xD8F2,0x5D8A,0x4C9F},
    {0x0000,0x4208,0x632C,0xFFFF,0x8410,0xFFFF,0x7BEF,0x39E7},
    {0x0011,0x0192,0x02B5,0x7FFF,0x03FF,0xF81F,0x07FF,0x001F},
    {0x0320,0x0540,0x06A0,0x07E0,0x03A0,0xFFE0,0x05C0,0x02A0},
    {0x0101,0x03E0,0x05E0,0x07E0,0x03E0,0x07FF,0x05E0,0x03E0},
    {0x0863,0x39E7,0x7BEF,0xFFFF,0x5ACB,0xFFE0,0x07E0,0x001F},
    {0x0841,0xAD55,0x3E68,0x3DDF,0x6B4D,0x946F,0x3E68,0x3DDF},
    {0x0000,0x0000,0x0000,0x0000,0x0000,0xF81F,0x0000,0x0000}
  };
  static const uint16_t dark[12][8] = {
    {0x0000,0x39C7,0x2104,0x7BEF,0x528A,0xF81F,0x0320,0x0010},
    {0x0000,0x2104,0x7BEF,0xBDF7,0x528A,0xF81F,0x0320,0x0010},
    {0x0000,0x2104,0x4208,0xBDF7,0x39C7,0x801F,0x2104,0x0010},
    {0x0000,0x0186,0x03EF,0x2D6B,0x2104,0xF81F,0x0320,0x0010},
    {0x0000,0x2104,0x39C7,0x7BEF,0x3186,0x801F,0x0320,0x0010},
    {0x0000,0x2104,0x4208,0xBDF7,0x3186,0xFFFF,0x39C7,0x2104},
    {0x0000,0x0108,0x0190,0x3DEF,0x01F0,0xF81F,0x03EF,0x0010},
    {0x0000,0x0120,0x01E0,0x03E0,0x01A0,0x7BEF,0x01C0,0x0120},
    {0x0000,0x0120,0x01C0,0x03E0,0x01A0,0x07FF,0x01C0,0x0120},
    {0x0000,0x2104,0x4208,0x7BEF,0x3186,0xFFE0,0x0320,0x0010},
    {0x0000,0x39C7,0x18C3,0x2104,0x3186,0xF81F,0x18C3,0x0010},
    {0x0000,0x0000,0x0000,0x0000,0x0000,0xF81F,0x0000,0x0000}
  };
  uint8_t theme = min(mapTheme, (uint8_t)11);
  if (theme == 10) {
    uint16_t pixel = mapPalette[color & 15];
    if (!nightOn) return pixel;
    uint8_t red = ((pixel >> 11) & 31) >> 1;
    uint8_t green = ((pixel >> 5) & 63) >> 1;
    uint8_t blue = (pixel & 31) >> 1;
    return (red << 11) | (green << 5) | blue;
  }
  return nightOn ? dark[theme][min(color, (uint8_t)7)] : day[theme][min(color, (uint8_t)7)];
}

static void drawMapBase() {
  lcd->fillScreen(BG);
  lcd->fillRect(0, 0, LCD_WIDTH, 36, 0x10A3);
  lcd->drawFastHLine(0, 36, LCD_WIDTH, 0x39C7);
  lcd->fillRect(0, 37, LCD_WIDTH, 122, 0x0841);
  if (!mapEnabled) text("MAP OFF", 138, 88, 1, MUTED);
  else if (mapTheme == 11) text("ROUTE ONLY", 126, 88, 1, GREEN);
  else if (!mapValid) text("WAITING FOR MAP", 95, 88, 1, MUTED);
}

static void drawLiveMap() {
  int32_t viewX = havePosition && positionFrame == mapFrame ? positionX : mapCenterX;
  int32_t viewY = havePosition && positionFrame == mapFrame ? positionY : mapCenterY;
  float heading = (havePosition && positionFrame == mapFrame ? positionHeading : 0) * 0.0174532925f;
  if (havePosition && positionFrame == mapFrame) {
    uint32_t elapsed = min((uint32_t)(millis() - positionAt + leadMs), (uint32_t)2500);
    float meters = positionSpeedKmh * (1000.0f / 3600.0f) * elapsed / 1000.0f;
    viewX += (int32_t)lroundf(meters * sinf(heading));
    viewY += (int32_t)lroundf(meters * cosf(heading));
  }
  float cs = cosf(heading), sn = sinf(heading);
  const int carY = 137;
  const float scale = 0.12f;
  for (int y = 37; y < 159; ++y) {
    float forward = (carY - y) / scale;
    for (int x = 0; x < MAP_SIZE; ++x) {
      float right = (x - 160) / scale;
      float worldX = viewX + right * cs + forward * sn;
      float worldY = viewY - right * sn + forward * cs;
      int sx = (int)((worldX - mapCenterX) / mapMetersPerPixel + 160);
      int sy = (int)(160 - (worldY - mapCenterY) / mapMetersPerPixel);
      uint8_t color = 0;
      if (mapTheme == 7) { sx = (sx / 3) * 3; sy = (sy / 3) * 3; }
      if (mapTheme != 11 && mapValid && (uint32_t)sx < MAP_SIZE && (uint32_t)sy < MAP_SIZE) {
        uint32_t index = (uint32_t)sy * MAP_SIZE + sx;
        uint8_t packed = mapPixels[index >> 1];
        color = (index & 1) ? packed >> 4 : packed & 15;
      }
      mapRow[x] = themedMapColor(color);
    }
    lcd->draw16bitRGBBitmap(0, y, mapRow, MAP_SIZE, 1);
  }
  if (ghostCount > 1 && ghostFrame == mapFrame && positionFrame == mapFrame) {
    for (uint16_t i = 1; i < ghostCount; ++i) {
      float dx0 = ghostOriginX + ghostPoints[2 * (i - 1)] - viewX;
      float dy0 = ghostOriginY + ghostPoints[2 * (i - 1) + 1] - viewY;
      float dx1 = ghostOriginX + ghostPoints[2 * i] - viewX;
      float dy1 = ghostOriginY + ghostPoints[2 * i + 1] - viewY;
      int x0 = 160 + (int)lroundf(scale * (dx0 * cs - dy0 * sn));
      int y0 = carY - (int)lroundf(scale * (dx0 * sn + dy0 * cs));
      int x1 = 160 + (int)lroundf(scale * (dx1 * cs - dy1 * sn));
      int y1 = carY - (int)lroundf(scale * (dx1 * sn + dy1 * cs));
      lcd->drawLine(x0, y0, x1, y1, ORANGE);
    }
  }
  if (routeCount > 1 && routeFrame == mapFrame && positionFrame == mapFrame) {
    float traveled = 0;
    for (uint16_t i = 1; i < routeCount; ++i) {
      int32_t wx0 = routeOriginX + routeX[i - 1], wy0 = routeOriginY + routeY[i - 1];
      int32_t wx1 = routeOriginX + routeX[i], wy1 = routeOriginY + routeY[i];
      float segmentLength = hypotf((float)(wx1 - wx0), (float)(wy1 - wy0));
      float dx0 = wx0 - viewX, dy0 = wy0 - viewY, dx1 = wx1 - viewX, dy1 = wy1 - viewY;
      int x0 = 160 + (int)lroundf(scale * (dx0 * cs - dy0 * sn));
      int y0 = carY - (int)lroundf(scale * (dx0 * sn + dy0 * cs));
      int x1 = 160 + (int)lroundf(scale * (dx1 * cs - dy1 * sn));
      int y1 = carY - (int)lroundf(scale * (dx1 * sn + dy1 * cs));
      uint16_t ink = themedMapColor(5);
      if (cleanOn || mapTheme == 11) ink = CYAN;
      bool completed = traveled + segmentLength <= tripProgressDone;
      bool drawSegment = !(trailMode == 2 && completed);
      if (trailMode == 1 && completed) ink = 0x7BEF;
      if (curveOn && !completed && i + 1 < routeCount) {
        float ax = wx0 - (routeOriginX + routeX[i - 1]), ay = wy0 - (routeOriginY + routeY[i - 1]);
        float bx = (routeOriginX + routeX[i + 1]) - wx1, by = (routeOriginY + routeY[i + 1]) - wy1;
        float dot = ax * bx + ay * by;
        float cross = ax * by - ay * bx;
        float turn = atan2f(fabsf(cross), dot) * 57.29578f;
        if (turn > 65) ink = RED;
        else if (turn > 28) ink = ORANGE;
        else if (turn > 12) ink = GREEN;
      }
      for (uint8_t t = 0; !cleanOn && t < trafficCount; ++t) {
        if (!completed && trafficFrom[t] < traveled + segmentLength && trafficTo[t] > traveled) {
          ink = trafficStatus[t] >= 4 ? 0x9000 : trafficStatus[t] == 3 ? 0xE8E4 : 0xFDA0;
          break;
        }
      }
      if (drawSegment) lcd->drawLine(x0, y0, x1, y1, ink);
      traveled += segmentLength;
    }
  }
  if (mapTheme == 6 || mapTheme == 11) {
    for (int x = 0; x < LCD_WIDTH; x += 32) lcd->drawFastVLine(x, 37, 122, mapTheme == 11 ? 0x03E0 : 0x0192);
    for (int y = 37; y < 159; y += 32) lcd->drawFastHLine(0, y, LCD_WIDTH, mapTheme == 11 ? 0x03E0 : 0x0192);
  } else if (mapTheme == 8) {
    for (int radius = 36; radius < 170; radius += 36) lcd->drawCircle(160, carY, radius, nightOn ? 0x03E0 : 0x05E0);
    int sweep = (millis() / 24) % 360;
    float angle = sweep * 0.0174532925f;
    lcd->drawLine(160, carY, 160 + (int)(160 * cosf(angle)), carY - (int)(160 * sinf(angle)), CYAN);
  } else if (mapTheme == 9) {
    int northX = 160 - (int)lroundf(13 * sinf(heading));
    int northY = carY - (int)lroundf(13 * cosf(heading));
    lcd->drawLine(160, carY, northX, northY, WHITE);
    text("N", northX - 2, northY - 4, 1, WHITE);
    if (routeCount > 1) {
      int x = 160 + (int)lroundf(scale * ((routeOriginX + routeX[routeCount - 1] - viewX) * cs - (routeOriginY + routeY[routeCount - 1] - viewY) * sn));
      int y = carY - (int)lroundf(scale * ((routeOriginX + routeX[routeCount - 1] - viewX) * sn + (routeOriginY + routeY[routeCount - 1] - viewY) * cs));
      if (x >= 3 && x < 317 && y >= 40 && y < 158) lcd->drawRect(x - 5, y - 5, 10, 10, ORANGE);
    }
  }
  if (routeCount > 1 && routeFrame == mapFrame && positionFrame == mapFrame) {
    float dx = routeOriginX + routeX[routeCount - 1] - viewX;
    float dy = routeOriginY + routeY[routeCount - 1] - viewY;
    int x = 160 + (int)lroundf(scale * (dx * cs - dy * sn));
    int y = carY - (int)lroundf(scale * (dx * sn + dy * cs));
    if (x >= 4 && x < 316 && y >= 42 && y < 158) {
      lcd->fillCircle(x, y, 5, BG);
      lcd->drawCircle(x, y, 5, ORANGE);
      lcd->fillCircle(x, y, 2, ORANGE);
    }
  }
  if (!cleanOn && routeFrame == mapFrame && positionFrame == mapFrame) {
    for (uint8_t i = 0; i < routeLightCount; ++i) {
      uint32_t traveled = tripProgressDone > 0 ? (uint32_t)tripProgressDone : 0;
      if (routeLightAlong[i] <= traveled) continue;
      float dx = routeLightX[i] - viewX, dy = routeLightY[i] - viewY;
      int x = 160 + (int)lroundf(scale * (dx * cs - dy * sn));
      int y = carY - (int)lroundf(scale * (dx * sn + dy * cs));
      if (x < 4 || x >= LCD_WIDTH - 4 || y < 42 || y >= 159) continue;
      lcd->fillRoundRect(x - 3, y - 5, 6, 10, 2, WHITE);
      lcd->fillCircle(x, y - 2, 1, RED);
      lcd->fillCircle(x, y + 2, 1, GREEN);
    }
  }
  if (!cleanOn && cameraFrame == mapFrame && positionFrame == mapFrame) {
    for (uint8_t i = 0; i < cameraPointCount; ++i) {
      float dx = cameraPoints[i].x - viewX, dy = cameraPoints[i].y - viewY;
      int x = 160 + (int)lroundf(scale * (dx * cs - dy * sn));
      int y = carY - (int)lroundf(scale * (dx * sn + dy * cs));
      if (x < 7 || x >= LCD_WIDTH - 7 || y < 44 || y >= 159) continue;
      if (cameraPoints[i].type >= 16) {
        lcd->fillTriangle(x, y - 7, x - 7, y + 6, x + 7, y + 6, ORANGE);
        text("!", x - 2, y - 1, 1, BG);
      } else if (cameraPoints[i].type == 3) {
        lcd->drawCircle(x, y, 7, RED);
        lcd->fillCircle(x, y, 3, ORANGE);
      } else if (cameraPoints[i].speed || cameraPoints[i].type == 2) {
        lcd->fillCircle(x, y, 7, ORANGE);
      } else {
        lcd->fillCircle(x, y, 6, CYAN);
        lcd->drawFastVLine(x, y - 3, 7, WHITE);
      }
      if (cameraPoints[i].speed) {
        char limit[5];
        snprintf(limit, sizeof(limit), "%d", cameraPoints[i].speed);
        text(limit, x - (cameraPoints[i].speed >= 100 ? 8 : 5), y - 3, 1, BG);
      }
    }
  }
  drawArrow(160, carY, 'S', WHITE);
}

static void drawMapData() {
  if (mapEnabled) drawLiveMap();
  else {
    lcd->fillRect(0, 37, LCD_WIDTH, 122, BG);
    if (navigating) {
      drawArrow(160, 90, turnCode, WHITE);
      char distance[16];
      snprintf(distance, sizeof(distance), "%d m", distanceM);
      text(distance, 142, 139, 2, CYAN);
    } else text("MAP OFF", 136, 88, 2, MUTED);
  }
  if (mapEnabled && appImages[0].data) {
    lcd->fillRoundRect(70, 63, 180, 25, 4, BG);
    drawTextImage(appImages[0], 66, 180);
  }
  lcd->fillRect(0, 0, 80, 36, 0x10A3);
  lcd->fillRect(220, 0, 100, 36, 0x10A3);
  text(bleConnected ? "PHONE" : "OFFLINE", 224, 13, 1, bleConnected ? GREEN : MUTED);
  lcd->fillRect(264, 132, 56, 24, 0x10A2);
  if (gpsWeak) text("GPS!", 268, 139, 1, ORANGE);

  lcd->fillRect(0, 160, 320, 38, BG);
  if (navigating) {
    drawArrow(27, 179, turnCode, CYAN);
    char line[30];
    snprintf(line, sizeof(line), "%s in %d m", turnCode == 'L' ? "LEFT" : turnCode == 'R' ? "RIGHT" : turnCode == 'U' ? "U-TURN" : "STRAIGHT", distanceM);
    text(line, 62, 168, 2, WHITE);
    if (speedLimit && speedKmh > speedLimit) lcd->fillCircle(300, 180, 7, RED);
  } else {
    text("READY  -  CONNECT PHONE", 14, 171, 2, MUTED);
  }

  lcd->fillRect(0, 0, 80, 36, 0x10A3);
  char speed[8];
  snprintf(speed, sizeof(speed), "%d", speedKmh);
  text(speed, 11, 8, 2, speedLimit && speedKmh > speedLimit ? RED : WHITE);
  text("km/h", 61, 16, 1, MUTED);
  if (laneCount) {
    int startX = (LCD_WIDTH - laneCount * 15) / 2;
    for (uint8_t i = 0; i < laneCount; ++i) {
      uint16_t ink = laneFg[i] == 0 ? MUTED : laneFg[i] == 1 ? WHITE : laneFg[i] == 2 ? CYAN : ORANGE;
      lcd->drawFastVLine(startX + i * 15 + 5, 43, 19, 0x7BEF);
      if (laneBg[i]) lcd->fillTriangle(startX + i * 15 + 5, 45, startX + i * 15, 57, startX + i * 15 + 10, 57, ink);
    }
  }
  char alerts[42] = "";
  if (!cleanOn && hasCamera) snprintf(alerts, sizeof(alerts), "CAM %d  %dm", cameraLimit, cameraDistance);
  if (!cleanOn && lightDistance >= 0) {
    size_t used = strlen(alerts);
    snprintf(alerts + used, sizeof(alerts) - used, "%sLIGHT %dm", used ? "  |  " : "", lightDistance);
  }
  if (alerts[0]) text(alerts, 8, 188, 1, ORANGE);
  if (cruiseOn) text("CRUISE", 132, 15, 1, CYAN);
  if (mapTheme == 11) text("DRAGON RADAR", 105, 15, 1, GREEN);
  if (cleanOn) text("CLEAN", 140, 15, 1, CYAN);
  if (progressOn && remainM > 0) {
    uint32_t total = (uint32_t)tripProgressDone + remainM;
    int width = total ? (int)((uint64_t)tripProgressDone * LCD_WIDTH / total) : 0;
    lcd->fillRect(0, 197, LCD_WIDTH, 3, PANEL);
    if (width > 0) lcd->fillRect(0, 197, width, 3, CYAN);
  }
  uint32_t now = millis();
  if (rerouteAt && now - rerouteAt > 20000) rerouteAt = 0;
  if (fasterAt && now - fasterAt > 30000) fasterAt = 0;
  if (rerouteAt) text("REROUTING...", 110, 68, 2, ORANGE);
  else if (fasterAt) {
    char hint[24];
    snprintf(hint, sizeof(hint), "FASTER ROUTE -%d min", fasterMinutes);
    text(hint, 76, 68, 1, GREEN);
  }
}

static void drawRouteBase() {
  lcd->fillScreen(BG);
  text("TRIP", 12, 12, 2, CYAN);
  lcd->drawFastHLine(12, 38, 296, 0x39C7);
  text("REMAINING", 16, 55, 1, MUTED);
  text("NEXT TURN", 16, 130, 1, MUTED);
}

static void drawRouteData() {
  lcd->fillRect(12, 72, 230, 48, BG);
  char value[24];
  if (remainM >= 1000) snprintf(value, sizeof(value), "%.1f km", remainM / 1000.0f);
  else snprintf(value, sizeof(value), "%d m", remainM);
  text(value, 16, 75, 4, WHITE);
  lcd->fillRect(10, 145, 300, 53, BG);
  drawArrow(35, 184, turnCode, ORANGE);
  snprintf(value, sizeof(value), "%d m", distanceM);
  text(value, 72, 169, 3, WHITE);
  if (eta > 0) {
    int hours = eta / 100;
    int minutes = eta % 100;
    snprintf(value, sizeof(value), "ETA  %02d:%02d", hours, minutes);
    text(value, 196, 184, 1, MUTED);
  }
  if (appImages[1].data) {
    lcd->fillRect(16, 40, 288, 26, BG);
    drawTextImage(appImages[1], 43);
  } else if (tripMeters >= 0) {
    lcd->fillRect(16, 40, 288, 22, BG);
    if (tripMeters >= 1000) snprintf(value, sizeof(value), "TRIP %.1f km", tripMeters / 1000.0f);
    else snprintf(value, sizeof(value), "TRIP %d m", tripMeters);
    text(value, 16, 43, 1, CYAN);
    if (tripMaxKmh >= 0) {
      snprintf(value, sizeof(value), "MAX %d", tripMaxKmh);
      text(value, 180, 43, 1, MUTED);
    }
  }
  if (tripLights > 0) {
    snprintf(value, sizeof(value), "%d LIGHTS", tripLights);
    text(value, 16, 188, 1, ORANGE);
  }
  if (progressOn && remainM > 0) {
    uint32_t total = (uint32_t)tripProgressDone + remainM;
    int width = total ? (int)((uint64_t)tripProgressDone * 288 / total) : 0;
    lcd->drawRoundRect(16, 194, 288, 8, 3, 0x39C7);
    if (width > 2) lcd->fillRoundRect(18, 196, width - 2, 4, 2, CYAN);
  }
}

static const char *weatherName(int icon) {
  static const char *names[] = {"CLEAR", "CLOUDS", "OVERCAST", "RAIN", "HEAVY RAIN", "STORM", "SNOW", "FOG", "SLEET", "DUST", "CLEAR", "CLOUDS"};
  return names[clampInt(icon, 0, 11)];
}

static void drawInfoBase() {
  lcd->fillScreen(BG);
  text("WEATHER & ALERTS", 12, 12, 2, CYAN);
  lcd->drawFastHLine(12, 38, 296, 0x39C7);
}

static void drawInfoData() {
  lcd->fillRect(0, 42, LCD_WIDTH, 156, BG);
  if (weatherView == 1) {
    text(weatherHourly ? "NEXT HOURS" : "NEXT DAYS", 16, 50, 1, MUTED);
    if (!weatherForecastCount) text("NO FORECAST DATA", 16, 92, 2, MUTED);
    int count = weatherForecastCount;
    for (int i = 0; i < count; ++i) {
      int x = count <= 4 ? 44 + i * (232 / max(1, count - 1)) : 28 + i * 53;
      if (i == 0 && count <= 1) x = 160;
      char label[8], value[12];
      if (weatherHourly) snprintf(label, sizeof(label), "%02d:00", weatherForecastHour[i]);
      else snprintf(label, sizeof(label), "DAY %d", i + 1);
      text(label, x - 18, 70, 1, i == 0 ? CYAN : MUTED);
      text(weatherName(weatherForecastIcon[i]), x - 20, 92, 1, WHITE);
      snprintf(value, sizeof(value), "%d C", weatherForecastHigh[i]);
      text(value, x - 12, 116, 1, WHITE);
      if (weatherHourly) {
        snprintf(value, sizeof(value), "%d%%", weatherForecastLow[i]);
        text(value, x - 10, 138, 1, weatherForecastLow[i] >= 40 ? CYAN : MUTED);
      } else {
        snprintf(value, sizeof(value), "LOW %d", weatherForecastLow[i]);
        text(value, x - 15, 138, 1, MUTED);
      }
    }
    text("TAP FOR ALERTS", 104, 178, 1, CYAN);
    return;
  }
  if (weatherView == 2) {
    text("WEATHER ALERTS", 18, 52, 2, WHITE);
    char line[32];
    snprintf(line, sizeof(line), "WARNINGS  %d", weatherWarningCount);
    text(line, 18, 84, 2, weatherWarningCount ? ORANGE : MUTED);
    if (appImages[9].data) drawTextImage(appImages[9], 112);
    if (appImages[10].data) drawTextImage(appImages[10], 138);
    if (appImages[11].data) drawTextImage(appImages[11], 164);
    if (!weatherWarningCount && !weatherWarningLevel) text("NO ACTIVE ALERTS", 18, 120, 1, GREEN);
    else if (weatherWarningLevel) {
      snprintf(line, sizeof(line), "ALERT LEVEL %d", weatherWarningLevel);
      text(line, 18, 103, 1, ORANGE);
    }
    text("TAP FOR CURRENT", 104, 178, 1, CYAN);
    return;
  }
  if (appImages[3].data) drawTextImage(appImages[3], 44);
  else if (weatherHave) {
    text(weatherName(weatherIcon), 16, 54, 2, WHITE);
  } else {
    text("NO WEATHER DATA", 16, 62, 2, MUTED);
  }
  if (appImages[4].data) drawTextImage(appImages[4], 77);
  char value[28];
  if (weatherHave) {
    snprintf(value, sizeof(value), "%d C", weatherTemp);
    text(value, 16, 106, 2, WHITE);
    if (weatherTime[0]) text(weatherTime, 78, 110, 1, MUTED);
  }
  if (weatherAqi >= 0) {
    snprintf(value, sizeof(value), "AQI %d", weatherAqi);
    text(value, 170, 106, 2, weatherAqi > 150 ? ORANGE : WHITE);
  }
  lcd->drawFastHLine(16, 132, 288, 0x39C7);
  snprintf(value, sizeof(value), "WARNINGS  %d", weatherWarningCount);
  text(value, 18, 138, 1, weatherWarningCount ? ORANGE : MUTED);
  if (appImages[8].data) drawTextImage(appImages[8], 120);
  if (appImages[9].data) drawTextImage(appImages[9], 143);
  else if (weatherWarningLevel) {
    snprintf(value, sizeof(value), "ALERT LEVEL %d", weatherWarningLevel);
    text(value, 18, 146, 1, ORANGE);
  }
  if (appImages[10].data) drawTextImage(appImages[10], 160);
  if (appImages[11].data) drawTextImage(appImages[11], 177);
  if (!appImages[10].data && !appImages[11].data && weatherRainCount) {
    text("RAIN", 18, 174, 1, MUTED);
    for (int i = 0; i < weatherRainCount; ++i) {
      int height = 3 + weatherRain[i] * 2;
      lcd->fillRect(68 + i * 17, 194 - height, 10, height, weatherRain[i] ? CYAN : 0x39C7);
    }
  }
  text(weatherForecastCount ? "TAP FOR FORECAST" : "NO FORECAST", 108, 182, 1, CYAN);
}

static void drawTextImage(const TextImage &image, int top, int maxWidth) {
  if (!image.data || !image.width || !image.height) return;
  int drawWidth = min((int)image.width, maxWidth);
  int sourceX = (image.width - drawWidth) / 2;
  int left = (LCD_WIDTH - drawWidth) / 2;
  int height = min((int)image.height, 198 - top);
  int bytesPerRow = (image.width + 1) / 2;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < drawWidth; ++x) {
      int sx = sourceX + x;
      uint8_t packed = image.data[y * bytesPerRow + sx / 2];
      uint8_t level = (sx & 1) ? packed & 15 : packed >> 4;
      if (!level) continue;
      uint8_t value = level * 17;
      uint16_t gray = ((uint16_t)(value >> 3) << 11) | ((uint16_t)(value >> 2) << 5) | (value >> 3);
      lcd->drawPixel(left + x, top + y, gray);
    }
  }
}

static void drawMusicBase() {
  lcd->fillScreen(BG);
  text("MUSIC", 12, 12, 2, CYAN);
  lcd->drawFastHLine(12, 38, 296, 0x39C7);
}

static void drawMusicData() {
  lcd->fillRect(12, 44, 296, 104, BG);
  lcd->fillRect(80, 190, 160, 18, BG);
  if (!musicHave) text("NO PLAYER", 110, 82, 2, MUTED);
  else {
    drawTextImage(appImages[5], 60);
    drawTextImage(appImages[6], 110);
    text(musicPlaying ? "PLAYING" : "PAUSED", 125, 142, 1, musicPlaying ? GREEN : MUTED);
  }
  uint16_t ink = musicHave ? WHITE : MUTED;
  lcd->fillCircle(67, 174, 21, PANEL);
  lcd->fillCircle(160, 174, 21, PANEL);
  lcd->fillCircle(253, 174, 21, PANEL);
  lcd->drawCircle(67, 174, 22, PANEL);
  lcd->drawTriangle(61, 174, 74, 165, 74, 183, ink);
  lcd->drawFastVLine(57, 165, 18, ink);
  lcd->drawCircle(160, 174, 22, PANEL);
  if (musicPlaying) {
    lcd->fillRect(153, 164, 5, 20, ink);
    lcd->fillRect(163, 164, 5, 20, ink);
  } else {
    lcd->fillTriangle(154, 163, 154, 185, 170, 174, ink);
  }
  lcd->drawCircle(253, 174, 22, PANEL);
  lcd->drawTriangle(259, 174, 246, 165, 246, 183, ink);
  lcd->drawFastVLine(263, 165, 18, ink);
  static const char *acks[] = {"", "", "SENDING", "PLAYBACK UPDATED", "PLAYER DID NOT RESPOND", "NO PLAYER"};
  if (musicAck) text(acks[musicAck], 88, 196, 1, musicAck == 3 ? GREEN : musicAck >= 4 ? ORANGE : MUTED);
}

static void drawCallScreen() {
  if (callInfoDirty) {
    lcd->fillRect(0, 48, LCD_WIDTH, 136, BG);
    if (appImages[2].data) {
      drawTextImage(appImages[2], 64);
      text(callNumber[0] ? callNumber : "UNKNOWN NUMBER", 16, 111, 2, WHITE);
    } else text(callNumber[0] ? callNumber : "UNKNOWN NUMBER", 16, 82, 2, WHITE);
    text(callAnswerSent ? "ANSWER REQUESTED" : "HOLD TO ANSWER", 16, 150, 2, MUTED);
    callInfoDirty = false;
  }
  lcd->fillRect(16, 190, 288, 16, BG);
  lcd->drawRoundRect(16, 190, 288, 16, 6, 0x39C7);
  if (callAnswerSent) lcd->fillRoundRect(18, 192, 284, 12, 4, GREEN);
  else if (callHoldAt) {
    int progress = min(284, (int)((millis() - callHoldAt) * 284 / 1200));
    if (progress > 0) lcd->fillRoundRect(18, 192, progress, 12, 4, GREEN);
  }
}

static void beginCalibration() {
  calibrating = true;
  calibrationStep = 0;
  memset(calibX, 0, sizeof(calibX));
  memset(calibY, 0, sizeof(calibY));
}

static void drawCalibrationTarget(uint8_t step) {
  static const int16_t targetX[] = {22, 298, 22, 298};
  static const int16_t targetY[] = {53, 53, 218, 218};
  int x = targetX[step];
  int y = targetY[step];
  lcd->drawCircle(x, y, 13, ORANGE);
  lcd->drawFastHLine(x - 17, y, 35, ORANGE);
  lcd->drawFastVLine(x, y - 17, 35, ORANGE);
}

static void drawSettings() {
  lcd->fillScreen(BG);
  text("MAP & DISPLAY", 12, 12, 2, CYAN);
  lcd->drawFastHLine(12, 38, 296, 0x39C7);
  static const char *themes[] = {"CLASSIC", "VICE CITY", "GTA 5", "NEON", "PAPER", "MINIMAL", "BLUEPRINT", "PIXEL", "RADAR", "COMPASS", "SATELLITE", "DRAGON"};
  const char *night = nightModeChoice == 0 ? "AUTO" : nightModeChoice == 1 ? "DAY" : "NIGHT";
  const char *trail = trailMode == 1 ? "GREY" : "ERASE";
  const char *onCurve = curveOn ? "ON" : "OFF";
  const char *clean = cleanOn ? "ON" : "OFF";
  const char *progress = progressOn ? "ON" : "OFF";
  const char *labels[] = {"MAP STYLE", "NIGHT MODE", "TRAVELED", "CURVE COLOR", "CLEAN MAP", "TRIP BAR", "TOUCH CALIBRATION"};
  const char *values[] = {themes[min(mapTheme, (uint8_t)11)], night, trail, onCurve, clean, progress, "START"};
  for (uint8_t i = 0; i < 7; ++i) {
    int y = 43 + i * 22;
    lcd->drawFastHLine(14, y + 21, 292, 0x2104);
    text(labels[i], 18, y + 6, 1, i == 6 ? ORANGE : WHITE);
    int w = strlen(values[i]) * 6;
    text(values[i], 296 - w, y + 6, 1, i == 6 ? ORANGE : CYAN);
  }
  if (calibrating) drawCalibrationTarget(calibrationStep);
}

static void render() {
  static bool renderedCall = false;
  bool fullRender = page != renderedPage || calibrating != renderedCalibrating || callOn != renderedCall || settingsDirty;
  if (!fullRender && !callOn && page == MAP_PAGE && millis() - lastMapDrawAt < 50) {
    mapRenderPending = true;
    return;
  }
  if (fullRender) {
    mapRenderPending = false;
    settingsDirty = false;
    renderedPage = page;
    renderedCalibrating = calibrating;
    renderedCall = callOn;
    if (!callOn) {
      if (calibrating || page == SETTINGS_PAGE) drawSettings();
      else if (page == MAP_PAGE) drawMapBase();
      else if (page == ROUTE_PAGE) drawRouteBase();
      else if (page == INFO_PAGE) drawInfoBase();
      else if (page == MUSIC_PAGE) drawMusicBase();
      if (!calibrating) {
        static const char *labels[] = {"MAP", "TRIP", "WX", "MUSIC", "SET"};
        for (uint8_t i = 0; i < 5; ++i) {
          int x = 2 + i * 64;
          lcd->fillRoundRect(x, 200, 60, 36, 5, (uint8_t)page == i ? CYAN : PANEL);
          text(labels[i], x + (i == 3 ? 12 : 18), 212, 1, WHITE);
        }
      }
    } else {
      lcd->fillScreen(BG);
      lcd->fillRect(0, 0, LCD_WIDTH, 42, 0x10A3);
      text("INCOMING CALL", 16, 14, 2, ORANGE);
      callInfoDirty = true;
    }
  }
  if (callOn) drawCallScreen();
  else if (calibrating) {
    if (fullRender) {
      renderedCalibrationStep = calibrationStep;
      drawCalibrationTarget(calibrationStep);
    } else if (renderedCalibrationStep != calibrationStep) {
      static const int16_t targetX[] = {22, 298, 22, 298};
      static const int16_t targetY[] = {53, 53, 218, 218};
      int oldX = targetX[renderedCalibrationStep];
      int oldY = targetY[renderedCalibrationStep];
      lcd->fillRect(oldX - 18, oldY - 18, 37, 37, BG);
      drawCalibrationTarget(calibrationStep);
      renderedCalibrationStep = calibrationStep;
    }
  } else if (page == MAP_PAGE) { lastMapDrawAt = millis(); drawMapData(); }
  else if (page == ROUTE_PAGE) drawRouteData();
  else if (page == INFO_PAGE) drawInfoData();
  else if (page == MUSIC_PAGE) drawMusicData();
}

static bool readTouchRaw(uint16_t &x, uint16_t &y) {
  if (digitalRead(TP_IRQ) != LOW) return false;
  uint32_t sumX = 0, sumY = 0;
  for (int i = 0; i < 5; ++i) {
    touchSPI.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TP_CS, LOW);
    touchSPI.transfer(0xD0);
    uint16_t rx = (uint16_t)touchSPI.transfer16(0) >> 3;
    touchSPI.transfer(0x90);
    uint16_t ry = (uint16_t)touchSPI.transfer16(0) >> 3;
    digitalWrite(TP_CS, HIGH);
    touchSPI.endTransaction();
    sumX += rx;
    sumY += ry;
  }
  x = sumX / 5;
  y = sumY / 5;
  return true;
}

static void calibrationTap(int x, int y) {
  calibX[calibrationStep] = x;
  calibY[calibrationStep] = y;
  if (++calibrationStep < 4) return;
  int rawXLeft = (calibX[0] + calibX[2]) / 2;
  int rawXRight = (calibX[1] + calibX[3]) / 2;
  int rawYLeft = (calibY[0] + calibY[2]) / 2;
  int rawYRight = (calibY[1] + calibY[3]) / 2;
  int rawXTop = (calibX[0] + calibX[1]) / 2;
  int rawXBottom = (calibX[2] + calibX[3]) / 2;
  int rawYTop = (calibY[0] + calibY[1]) / 2;
  int rawYBottom = (calibY[2] + calibY[3]) / 2;
  bool rawXTracksScreenX = abs(rawXRight - rawXLeft) >= abs(rawYRight - rawYLeft);
  uint16_t screenXLeft = rawXTracksScreenX ? rawXLeft : (calibY[0] + calibY[2]) / 2;
  uint16_t screenXRight = rawXTracksScreenX ? rawXRight : (calibY[1] + calibY[3]) / 2;
  uint16_t screenYTop = rawXTracksScreenX ? rawYTop : rawXTop;
  uint16_t screenYBottom = rawXTracksScreenX ? rawYBottom : rawXBottom;
  rawXMin = min(screenXLeft, screenXRight);
  rawXMax = max(screenXLeft, screenXRight);
  rawYMin = min(screenYTop, screenYBottom);
  rawYMax = max(screenYTop, screenYBottom);
  touchSwapXY = !rawXTracksScreenX;
  reverseTouchX = screenXLeft > screenXRight;
  reverseTouchY = screenYTop > screenYBottom;
  if (rawXMax - rawXMin < 800 || rawYMax - rawYMin < 800) {
    rawXMin = rawYMin = 250;
    rawXMax = rawYMax = 3850;
    reverseTouchX = reverseTouchY = touchSwapXY = false;
  }
  Preferences prefs;
  prefs.begin("cyd-touch", false);
  prefs.putUShort("xmin", rawXMin);
  prefs.putUShort("xmax", rawXMax);
  prefs.putUShort("ymin", rawYMin);
  prefs.putUShort("ymax", rawYMax);
  prefs.putBool("revx", reverseTouchX);
  prefs.putBool("revy", reverseTouchY);
  prefs.putBool("swap", touchSwapXY);
  prefs.end();
  calibrating = false;
}

static void touchLoop() {
  uint16_t rx, ry;
  bool down = readTouchRaw(rx, ry);
  if (!down) {
    bool redrawCall = callOn && callHoldAt && !callAnswerSent;
    touchWasDown = false;
    touchSwipeHandled = false;
    callHoldAt = 0;
    if (redrawCall) render();
    return;
  }
  if (touchSwapXY) { uint16_t value = rx; rx = ry; ry = value; }
  int x = clampInt(map(rx, rawXMin, rawXMax, reverseTouchX ? 298 : 22, reverseTouchX ? 22 : 298), 0, LCD_WIDTH - 1);
  int y = clampInt(map(ry, rawYMin, rawYMax, reverseTouchY ? 218 : 53, reverseTouchY ? 53 : 218), 0, LCD_HEIGHT - 1);
  if (!touchWasDown) {
    touchWasDown = true;
    touchSwipeHandled = false;
    touchDownX = x;
    touchDownY = y;
    notifyTouch(x, y);
    Page previousPage = page;
    if (callOn) {
      callHoldAt = millis();
      render();
      return;
    } else if (calibrating) {
      calibrationTap(rx, ry);
    } else if (y >= 198) {
      page = x < 64 ? MAP_PAGE : x < 128 ? ROUTE_PAGE : x < 192 ? INFO_PAGE : x < 256 ? MUSIC_PAGE : SETTINGS_PAGE;
      if (page == INFO_PAGE) weatherView = 0;
    } else if (page == SETTINGS_PAGE && y >= 43 && y < 198) {
      uint8_t row = min((y - 43) / 22, 6);
      if (row == 0) {
        mapTheme = (mapTheme + 1) % 12;
        sendSettingEvent(1, mapTheme);
      } else if (row == 1) {
        nightModeChoice = (nightModeChoice + 1) % 3;
        if (nightModeChoice != 0) nightOn = nightModeChoice == 2;
        sendSettingEvent(2, nightModeChoice);
      } else if (row == 2) {
        trailMode = trailMode == 1 ? 2 : 1;
        sendSettingEvent(3, trailMode);
      } else if (row == 3) {
        curveOn = !curveOn;
        sendSettingEvent(4, curveOn);
      } else if (row == 4) {
        cleanOn = !cleanOn;
        sendSettingEvent(5, cleanOn);
      } else if (row == 5) {
        progressOn = !progressOn;
        sendSettingEvent(6, progressOn);
      } else beginCalibration();
      settingsDirty = true;
    } else if (page == MUSIC_PAGE && y >= 148) {
      sendMusicAction(x < 115 ? 'P' : x < 205 ? 'T' : 'N');
      musicAck = 1;
    } else if (page == INFO_PAGE && y >= 42 && y < 198) {
      weatherView = (weatherView + 1) % 3;
    }
    if (!calibrating && page != previousPage && page == MUSIC_PAGE) sendPhoneEvent("T,M,Q");
    if (!calibrating && page != previousPage && page == INFO_PAGE) sendPhoneEvent("T,W");
    render();
    return;
  }
  int dx = x - touchDownX;
  int dy = y - touchDownY;
  if (callOn && !callAnswerSent && callHoldAt && millis() - callHoldAt >= 1200) {
    if (txChar && bleConnected) {
      const char answer[] = "T,A";
      txChar->setValue((const uint8_t *)answer, sizeof(answer) - 1);
      txChar->notify();
      callAnswerSent = true;
      callInfoDirty = true;
      callHoldAt = 0;
      render();
    }
    return;
  }
  if (!callOn && !touchSwipeHandled && abs(dx) > 55 && abs(dx) > abs(dy)) {
    if (page == SETTINGS_PAGE) page = MAP_PAGE;
    else page = (Page)(((uint8_t)page + (dx < 0 ? 1 : 3)) % 4);
    if (page == INFO_PAGE) { weatherView = 0; sendPhoneEvent("T,W"); }
    if (page == MUSIC_PAGE) sendPhoneEvent("T,M,Q");
    touchSwipeHandled = true;
    render();
  }
}

static void processIncoming() {
  bool changed = false;
  for (uint8_t count = 0; count < RX_SLOTS; ++count) {
    uint8_t packet[RX_PACKET_SIZE];
    uint16_t length = 0;
    portENTER_CRITICAL(&rxMux);
    if (rxTail != rxHead) {
      length = rxLengths[rxTail];
      memcpy(packet, rxPackets[rxTail], length);
      rxTail = (rxTail + 1) % RX_SLOTS;
    }
    portEXIT_CRITICAL(&rxMux);
    if (!length) break;
    if (packet[0] == 'M') {
      changed = handleBulk(packet, length) || changed;
    } else if (packet[0] == 'B') {
      changed = handleImage(packet, length) || changed;
    } else {
      char message[RX_PACKET_SIZE + 1];
      memcpy(message, packet, length);
      message[length] = '\0';
      parseMessage(message);
      changed = true;
    }
  }
  if (changed) render();
}

static void startBluetooth() {
  BLEDevice::init("MotoNav-CYD");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *rx = service->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  txChar = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, screenBrightness);
  pinMode(TP_CS, OUTPUT);
  digitalWrite(TP_CS, HIGH);
  pinMode(TP_IRQ, INPUT);
  touchSPI.begin(TP_SCLK, TP_MISO, TP_MOSI, TP_CS);
  if (!lcd->begin(40000000)) Serial.println("LCD begin failed");
  lcd->setRotation(1);
  Preferences prefs;
  prefs.begin("cyd-touch", true);
  rawXMin = prefs.getUShort("xmin", 250);
  rawXMax = prefs.getUShort("xmax", 3850);
  rawYMin = prefs.getUShort("ymin", 250);
  rawYMax = prefs.getUShort("ymax", 3850);
  reverseTouchX = prefs.getBool("revx", false);
  reverseTouchY = prefs.getBool("revy", false);
  touchSwapXY = prefs.getBool("swap", false);
  prefs.end();
  startBluetooth();
  render();
  Serial.printf("MotoNav ready, firmware %s display=%dx%d touch=XPT2046\n", FW_VERSION, LCD_WIDTH, LCD_HEIGHT);
}

void loop() {
  processIncoming();
  touchLoop();
  if (mapRenderPending && millis() - lastMapDrawAt >= 50) render();
  if (callOn && (!bleConnected || millis() - callSeenAt > 120000)) {
    callOn = false;
    callNumber[0] = '\0';
    render();
  } else if (callOn && callHoldAt && !callAnswerSent) {
    static uint32_t lastCallProgress = 0;
    if (millis() - lastCallProgress >= 70) { lastCallProgress = millis(); render(); }
  }
  delay(4);
}
