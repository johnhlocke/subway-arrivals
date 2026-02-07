// subway_display.ino
// ESP32-S3 + 2.9" Tri-Color eInk + NeoPixel subway arrivals display
//
// Hardware:
//   - Adafruit ESP32-S3 Reverse TFT Feather (Product 5691)
//   - Adafruit 2.9" Tri-Color eInk Breakout (Product 1028) via SPI
//   - Adafruit NeoPixel Strip (~10 LEDs) on GPIO 11
//
// Libraries (install via Arduino Library Manager):
//   - Adafruit GFX Library
//   - Adafruit EPD (provides Adafruit_ThinkInk.h)
//   - Adafruit NeoPixel
//   - ArduinoJson

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Adafruit_ThinkInk.h"
#include <Adafruit_NeoPixel.h>

// ---- USER CONFIG ----
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
const char* API_URL       = "http://YOUR_NAS_IP:5002/api/arrivals";
// ---------------------

// NeoPixel
#define NEOPIXEL_PIN  11
#define NUM_LEDS      10
Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// eInk display — SSD1680, 296x128, tri-color
// Shares SPI bus (SCK=36, MOSI=35) with the built-in TFT
#define EPD_CS   5
#define EPD_DC   6
#define EPD_RST  9
#define EPD_BUSY 10
ThinkInk_290_Tricolor_Z10 display(EPD_DC, EPD_RST, EPD_CS, -1, EPD_BUSY);

// Timing
#define POLL_INTERVAL_MS 30000
unsigned long lastPollMs = 0;

// State tracking for eInk refresh (only refresh when data changes)
#define MAX_ARRIVALS 10
int prevArrivals[MAX_ARRIVALS];
int prevArrivalCount = -1; // -1 = never drawn
int prevAdvisoryState = -1; // -1 = never, 0 = none, 1 = leave now, 2 = wait
int prevWaitHome = -1;
String prevUpdated = "";

// Walk time (from API)
int walkMinutes = 10;

// ---- Advisory logic (mirrors the web frontend) ----
// Returns:  0 = no catchable train
//           1 = LEAVE NOW
//           2 = WAIT N MIN
// Sets outTrainMin, outPlatformWait, outWaitHome
int computeAdvisory(int* arrivals, int count, int walk,
                    int &outTrainMin, int &outPlatformWait, int &outWaitHome) {
  for (int i = 0; i < count; i++) {
    int platformWait = arrivals[i] - walk;
    if (platformWait < 1) continue;
    outTrainMin = arrivals[i];
    outPlatformWait = platformWait;
    if (platformWait < 4) {
      outWaitHome = 0;
      return 1; // LEAVE NOW
    } else {
      outWaitHome = platformWait - 4;
      return 2; // WAIT
    }
  }
  return 0; // no catchable train
}

// ---- NeoPixel helpers ----
void setStripColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void stripOff() {
  strip.clear();
  strip.show();
}

// ---- eInk drawing ----
void drawDisplay(int* arrivals, int count, int advisoryState,
                 int trainMin, int platformWait, int waitHome,
                 const char* updated) {
  display.clearBuffer();

  // Layout constants (296 wide x 128 tall, landscape)
  const int LEFT_MARGIN = 4;
  int y = 0;

  // --- Advisory banner ---
  if (advisoryState == 1) {
    // LEAVE NOW — red text
    display.setTextColor(EPD_RED);
    display.setTextSize(3);
    display.setCursor(LEFT_MARGIN, y + 2);
    display.print("LEAVE NOW");
    y += 28;
    // Detail line
    display.setTextColor(EPD_BLACK);
    display.setTextSize(1);
    display.setCursor(LEFT_MARGIN, y);
    char detail[64];
    snprintf(detail, sizeof(detail), "Train in %d min - %d min on platform", trainMin, platformWait);
    display.print(detail);
    y += 14;
  } else if (advisoryState == 2) {
    // WAIT N MIN — black text
    display.setTextColor(EPD_BLACK);
    display.setTextSize(3);
    display.setCursor(LEFT_MARGIN, y + 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "WAIT %d MIN", waitHome);
    display.print(buf);
    y += 28;
    // Detail line
    display.setTextSize(1);
    display.setCursor(LEFT_MARGIN, y);
    char detail[64];
    snprintf(detail, sizeof(detail), "Train in %d min - %d min on platform", trainMin, platformWait);
    display.print(detail);
    y += 14;
  } else {
    // No advisory — skip banner area
    y += 4;
  }

  // --- Divider ---
  display.drawLine(0, y, 295, y, EPD_BLACK);
  y += 4;

  // --- Arrival rows (up to 4) ---
  display.setTextColor(EPD_BLACK);
  display.setTextSize(2);
  int shown = count < 4 ? count : 4;
  for (int i = 0; i < shown; i++) {
    // Circle badge with "A"
    int badgeCenterX = LEFT_MARGIN + 10;
    int badgeCenterY = y + 8;
    display.fillCircle(badgeCenterX, badgeCenterY, 9, EPD_BLACK);
    display.setTextColor(EPD_WHITE);
    display.setTextSize(1);
    display.setCursor(badgeCenterX - 3, badgeCenterY - 3);
    display.print("A");

    // Arrival time
    display.setTextColor(EPD_BLACK);
    display.setTextSize(2);
    display.setCursor(LEFT_MARGIN + 28, y + 1);
    char row[16];
    if (arrivals[i] < 1) {
      snprintf(row, sizeof(row), "now");
    } else {
      snprintf(row, sizeof(row), "%d min", arrivals[i]);
    }
    display.print(row);
    y += 22;
  }

  if (count == 0) {
    display.setTextSize(1);
    display.setCursor(LEFT_MARGIN, y + 4);
    display.print("No upcoming arrivals");
    y += 16;
  }

  // --- Divider ---
  display.drawLine(0, y + 2, 295, y + 2, EPD_BLACK);

  // --- Updated timestamp at bottom ---
  display.setTextColor(EPD_BLACK);
  display.setTextSize(1);
  display.setCursor(LEFT_MARGIN, 118);
  char ts[32];
  snprintf(ts, sizeof(ts), "Updated %s", updated);
  display.print(ts);

  display.display();
}

// ---- Check if eInk needs refresh ----
bool displayChanged(int* arrivals, int count, int advisoryState, int waitHome, const String &updated) {
  if (count != prevArrivalCount) return true;
  if (advisoryState != prevAdvisoryState) return true;
  if (advisoryState == 2 && waitHome != prevWaitHome) return true;
  if (updated != prevUpdated) return true;
  for (int i = 0; i < count && i < MAX_ARRIVALS; i++) {
    if (arrivals[i] != prevArrivals[i]) return true;
  }
  return false;
}

void saveState(int* arrivals, int count, int advisoryState, int waitHome, const String &updated) {
  prevArrivalCount = count;
  prevAdvisoryState = advisoryState;
  prevWaitHome = waitHome;
  prevUpdated = updated;
  for (int i = 0; i < count && i < MAX_ARRIVALS; i++) {
    prevArrivals[i] = arrivals[i];
  }
}

// ---- WiFi ----
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// ---- Main ----
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Subway Display starting...");

  // NeoPixel init
  strip.begin();
  strip.setBrightness(40); // gentle glow
  stripOff();

  // eInk init
  display.begin(THINKINK_TRICOLOR);
  display.setRotation(1); // landscape, 296 wide x 128 tall
  display.clearBuffer();
  display.setTextColor(EPD_BLACK);
  display.setTextSize(2);
  display.setCursor(10, 50);
  display.print("Connecting...");
  display.display();

  // WiFi
  connectWiFi();

  // Force immediate first poll
  lastPollMs = 0;
}

void loop() {
  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    connectWiFi();
  }

  unsigned long now = millis();
  if (now - lastPollMs < POLL_INTERVAL_MS && lastPollMs != 0) {
    delay(100);
    return;
  }
  lastPollMs = now;

  // HTTP GET
  HTTPClient http;
  http.begin(API_URL);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();
  Serial.println("API response: " + payload);

  // Parse JSON
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  JsonArray arr = doc["arrivals"].as<JsonArray>();
  String updated = doc["updated"].as<String>();
  walkMinutes = doc["walk_minutes"] | 10;

  int arrivals[MAX_ARRIVALS];
  int count = 0;
  for (JsonObject a : arr) {
    if (count >= MAX_ARRIVALS) break;
    arrivals[count++] = a["minutes"].as<int>();
  }

  // Advisory logic
  int trainMin = 0, platformWait = 0, waitHome = 0;
  int advisoryState = computeAdvisory(arrivals, count, walkMinutes,
                                       trainMin, platformWait, waitHome);

  // NeoPixel — update every cycle (instant)
  switch (advisoryState) {
    case 1:  setStripColor(0, 80, 0);   break; // LEAVE NOW → green
    case 2:  setStripColor(80, 0, 0);   break; // WAIT → red
    default: stripOff();                 break; // no catchable train → off
  }

  // eInk — only refresh if data changed (refresh takes ~15s)
  if (displayChanged(arrivals, count, advisoryState, waitHome, updated)) {
    Serial.println("Display data changed, refreshing eInk...");
    drawDisplay(arrivals, count, advisoryState, trainMin, platformWait, waitHome, updated.c_str());
    saveState(arrivals, count, advisoryState, waitHome, updated);
    Serial.println("eInk refresh complete.");
  } else {
    Serial.println("No display change, skipping eInk refresh.");
  }
}
