// ESP32 program to countdown hours remaining until 2026 April 12 06:00 (local Hungarian time)
// Prints remaining hours periodically to Serial.
// WiFi credentials provided by user.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <TM1637Display.h>

// ---- Configuration ----
static const char* WIFI_SSID     = "WIFISSID";
static const char* WIFI_PASSWORD = "WIFIPASS";
// Target date/time (local time Europe/Budapest with DST rules applied)
struct TargetDate { int year; int month; int day; int hour; int minute; int second; };
static const TargetDate TARGET = {2026, 4, 12, 6, 0, 0};

// How often to print update (ms)
static const unsigned long PRINT_INTERVAL_MS = 60UL * 1000UL; // every minute
static const unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // 6 hours
static const unsigned long DISPLAY_MODE_DURATION_MS = 15000UL; // 15 seconds per mode
unsigned long lastPrint = 0;
long lastReportedHours = -1;
unsigned long lastNtpSync = 0;
unsigned long lastModeSwitch = 0;

enum DisplayMode { MODE_HOURS = 0, MODE_DAYS = 1 };
DisplayMode displayMode = MODE_HOURS;

// TM1637 pins
static const uint8_t TM_CLK_PIN = 32; // GPIO32
static const uint8_t TM_DIO_PIN = 33; // GPIO33
TM1637Display display(TM_CLK_PIN, TM_DIO_PIN);
bool displayInitialized = false;

void initDisplay() {
  if (displayInitialized) return;
  display.setBrightness(0x0f, true); // max brightness
  display.clear();
  displayInitialized = true;
}

void showValueOnDisplay(long value) {
  if (!displayInitialized) initDisplay();
  if (value < 0) {
    const uint8_t SEG_DASH = 0x40; // segment g
    uint8_t data[] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
    display.setSegments(data);
    return;
  }
  if (value > 9999) value = 9999; // clamp
  display.showNumberDec(value, false); // no leading zeros
}

void updateDisplay(long hoursRemaining) {
  if (hoursRemaining < 0) {
    showValueOnDisplay(-1);
    return;
  }
  long days = hoursRemaining / 24;
  if (displayMode == MODE_HOURS) {
    showValueOnDisplay(hoursRemaining-1);
  } else {
    showValueOnDisplay(days);
  }
}

// Compute hours remaining until target; returns -1 if time not yet synced.
long computeHoursRemaining() {
  time_t now = time(nullptr);
  if (now < 100000) return -1; // time not set yet (still Jan 1970)

  struct tm target = {0};
  target.tm_year = TARGET.year - 1900;
  target.tm_mon  = TARGET.month - 1;
  target.tm_mday = TARGET.day;
  target.tm_hour = TARGET.hour;
  target.tm_min  = TARGET.minute;
  target.tm_sec  = TARGET.second;
  target.tm_isdst = -1; // let library determine DST for Europe/Budapest

  time_t targetTime = mktime(&target); // interprets as local time per TZ
  if (targetTime <= now) return 0;
  long seconds = (long) difftime(targetTime, now);
  return seconds / 3600L; // floor
}

void reportIfNeeded(bool force = false) {
  long hours = computeHoursRemaining();
  if (hours < 0) {
    if (force) Serial.println(F("[INFO] Waiting for time sync..."));
  updateDisplay(-1);
    return;
  }
  if (force || hours != lastReportedHours) {
    // Also compute days/hours remainder for readability
    long days = hours / 24;
    long remHours = hours % 24;
    Serial.printf("[RESULT] Hours left until 2026-04-12 06:00: %ld (≈ %ld days %ld h)\n", hours-1, days, remHours);
    lastReportedHours = hours;
  updateDisplay(hours);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[INFO] Connecting to WiFi SSID '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[INFO] WiFi connected. IP: "));
    Serial.println(WiFi.localIP());
  // Set timezone to Europe/Budapest with DST rules
  // POSIX TZ: std offset dst[,start[/time],end[/time]]  CET-1CEST,M3.5.0/2,M10.5.0/3
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  lastNtpSync = millis();
  } else {
    Serial.println(F("[ERROR] WiFi connection failed."));
  }
}

void ensureTimeResync() {
  // If time never set or interval passed, trigger a resync
  time_t now = time(nullptr);
  bool timeValid = now >= 100000; // arbitrary threshold
  if (WiFi.status() == WL_CONNECTED) {
    if (!timeValid && (millis() - lastNtpSync) > 15000) { // after 15s still invalid
      Serial.println(F("[INFO] Retrying initial NTP sync..."));
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      lastNtpSync = millis();
    } else if ((millis() - lastNtpSync) > NTP_RESYNC_INTERVAL_MS) {
      Serial.println(F("[INFO] Periodic NTP resync..."));
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      lastNtpSync = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Booting..."));
  initDisplay();
  updateDisplay(-1); // indicate waiting
  connectWiFi();
  lastPrint = millis();
  lastModeSwitch = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  ensureTimeResync();
  unsigned long nowMs = millis();
  if (nowMs - lastPrint >= PRINT_INTERVAL_MS) {
    reportIfNeeded();
    lastPrint = nowMs;
  }
  // Alternate display between hours and days every DISPLAY_MODE_DURATION_MS
  if (nowMs - lastModeSwitch >= DISPLAY_MODE_DURATION_MS) {
    displayMode = (displayMode == MODE_HOURS) ? MODE_DAYS : MODE_HOURS;
    lastModeSwitch = nowMs;
    // Force refresh with current hours (does not reprint Serial unless changed)
    long hrs = computeHoursRemaining();
    updateDisplay(hrs);
  }
  // Force a report early after sync; attempt every second until we have valid time then switch to interval
  if (lastReportedHours < 0) {
    reportIfNeeded(true);
  }
  delay(1000);
}