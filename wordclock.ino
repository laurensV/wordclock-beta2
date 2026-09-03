// standard libraries
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <time.h>
#include <coredecls.h>
#include <TZ.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

// libraries from library manager
#include <DS3231.h> // https://github.com/NorthernWidget/DS3231
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Adafruit_NeoPixel.h> // https://github.com/adafruit/Adafruit_NeoPixel
#include "ArduinoJson.h" // https://github.com/bblanchon/ArduinoJson

// own services
#include "wordclock.h"
#include "src/filesystem.h"
#include "src/udplogger.h"
#include "src/esp8266fota.h"
#include "src/ledmatrix.h"

int VERSION = {
#include "VERSION.h"  
};

// Helper function to strip _INDEX from timezone string
String getActualPOSIXString(const char* indexedTZ) {
  String tz = String(indexedTZ);
  int lastUnderscore = tz.lastIndexOf('_');
  if (lastUnderscore > 0 && lastUnderscore < tz.length() - 1) {
    // Check if characters after '_' are all digits
    bool allDigits = true;
    for (int i = lastUnderscore + 1; i < tz.length(); i++) {
      if (!isDigit(tz.charAt(i))) {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      return tz.substring(0, lastUnderscore);
    }
  }
  return tz; // Return original if no valid _INDEX found
}

// ----------------------------------------------------------------------------------
//                                    GLOBAL VARIABLES
// ----------------------------------------------------------------------------------
long lastReadTime = 0, lastCheckUpdate = 0, lastStoreColors = 0, lastCheckNTP = 0;
bool storeColorTime = false, storeColorName = false, storeColorIcon = false;
WiFiManager wifiManager;
UDPLogger logger;
DS3231 rtc;
esp8266FOTA* FOTA;
int fileSystemVersion;
Adafruit_NeoPixel* pixels;
LEDMatrix* matrix;
ESP8266WebServer server(80);
bool NTPWorking = true;
uint32_t rainbowHue = 0;

// ----------------------------------------------------------------------------------
//                                        SETUP
// ----------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nSketchname: %s\nBuild: %s\n", (__FILE__), (__TIMESTAMP__));
  Serial.print("Version: "); Serial.println(VERSION);
  setupPersistentVars();
  setupFileSystem();
  setupTime();
  setupPixels(true);
  setupWifiManager();
}

// ----------------------------------------------------------------------------------
//                                        LOOP
// ----------------------------------------------------------------------------------

void loop() {
  if (wifiManager.process()) {
    print("Connected to WiFi");
    onWifiConnect();
  }
  ArduinoOTA.handle();
  server.handleClient();

  if (readTimeInterval()) {
    time_t timeNowUTC;
    struct tm* timeInfo;
    timeNowUTC = (RTClib::now()).unixtime(); // time(nullptr);
    timeInfo = localtime(&timeNowUTC);
    int hours = timeInfo->tm_hour;
    int minutes = timeInfo->tm_min;
    setNightMode(hours * 60 + minutes);
    String timeString;
    switch (mode) {
      case WORD_CLOCK:
      case RAINBOW:
        if (clockLayout.indexOf("OCLOCK", 0) >= 0) {
          timeString = timeToStringEnglish(hours, minutes);
        } else {
          timeString = timeToStringDutch(hours, minutes);
        }
        print(timeString);
        showTimeString(timeString);
        break;
      case DIGITAL_CLOCK:
        print(String(hours) + ":" + String(minutes));
        showDigitalTime(hours, minutes);
        break;
    }
    checkWifiDisconnect();
  }
  switch (mode) {
    case RAINBOW:
      for (int i = 0; i < pixels->numPixels(); i++) {  // For each pixel in strip...
        int pixelHue = rainbowHue + (i * 65536L / pixels->numPixels());
        if (matrix->leds[matrix->pixelRow(i)][matrix->pixelCol(i)] != LEDMatrix::TIME &&
          matrix->leds[matrix->pixelRow(i)][matrix->pixelCol(i)] != LEDMatrix::ICON &&
          matrix->leds[matrix->pixelRow(i)][matrix->pixelCol(i)] != LEDMatrix::NAME) {
          pixels->setPixelColor(i, pixels->gamma32(pixels->ColorHSV(pixelHue)));
        }
      }
      pixels->show();
      rainbowHue += 40; // Advance just a little along the color wheel
      break;
  }

  if (storeColorsInterval()) {
    if (storeColorTime) {
      EEPROM.put(ADR_COLOR_TIME, color_TIME);
      storeColorTime = false;
    }
    if (storeColorName) {
      EEPROM.put(ADR_COLOR_NAME, color_NAME);
      storeColorName = false;
    }
    if (storeColorIcon) {
      EEPROM.put(ADR_COLOR_ICON, color_ICON);
      storeColorIcon = false;
    }
    EEPROM.commit();
  }

  if (checkNTPInterval()) {
    if (isWifiConnected()) {
      if (!NTPWorking) {
        print("NTP update not working, restarting..");
        // WiFi.reconnect();
        ESP.restart();
      }
      NTPWorking = false;
    }
  }

  if (checkUpdateInterval() && AUTO_UPDATE) {
    if (isWifiConnected()) {
      print("checking for new updates..");
      bool updatedNeeded = FOTA->execHTTPcheck();
      if (updatedNeeded) {
        print("New update available!");
        FOTA->execOTA();
      } else {
        print("Already running latest version");
      }
    }
  }
}


void setupFileSystem() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
  }
  fileSystemVersion = (load_from_file("VERSION.txt")).toInt();
  print("filesystem version: ", false); print(fileSystemVersion);
}

void setupPixels(bool showAnimation) {
  pixels = new Adafruit_NeoPixel((clockWidth * clockHeight), NEOPIXEL_PIN);
  matrix = new LEDMatrix(pixels);
  pixels->begin();
  pixels->setBrightness(brightness);
  pixels->clear();
  if (showAnimation) {
    for (int i = 0; i < (clockWidth * clockHeight); i++) {
      uint16_t hue = (i * 65536) / (clockWidth * clockHeight);
      uint32_t color = Adafruit_NeoPixel::ColorHSV(hue);
      color = pixels->gamma32(color);
      pixels->setPixelColor(i, color);
      pixels->show();
      delay(20);
    }
  }
}

void setupPersistentVars() {
  //Init EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Uncomment and run it once, if you want to erase stored info
  // resetEEPROM();

  EEPROM.get(ADR_CLOCK_WIDTH, clockWidth);
  EEPROM.get(ADR_CLOCK_HEIGHT, clockHeight);
  clockLayout = "";

  if (!clockWidth || !clockHeight) {
    clockWidth = 13;
    clockHeight = 13;
  } else {
    for (int i = 0; i < (clockWidth * clockHeight); ++i) {
      clockLayout += char(EEPROM.read(ADR_CLOCK_LAYOUT + i));
    }
  }

  EEPROM.get(ADR_MODE, mode);

  EEPROM.get(ADR_BRIGHTNESS, brightness);
  if (!brightness) {
    brightness = 255;
  }
  EEPROM.get(ADR_NM_BRIGHTNESS, nightModeBrightness);
  if (!nightModeBrightness) {
    nightModeBrightness = 0;
  }

  EEPROM.get(ADR_COLOR_TIME, color_TIME);
  if (!color_TIME) {
    color_TIME = WHITE;
  }
  EEPROM.get(ADR_COLOR_NAME, color_NAME);
  if (!color_NAME) {
    color_NAME = WHITE;
  }
  EEPROM.get(ADR_COLOR_ICON, color_ICON);
  if (!color_ICON) {
    color_ICON = WHITE;
  }

  EEPROM.get(ADR_NM, checkNightMode);
  EEPROM.get(ADR_NM_START_H, nightModeStartHour);
  EEPROM.get(ADR_NM_START_M, nightModeStartMin);
  EEPROM.get(ADR_NM_END_H, nightModeEndHour);
  EEPROM.get(ADR_NM_END_M, nightModeEndMin);
  if (!nightModeStartHour && !nightModeStartMin && !nightModeEndHour && !nightModeEndMin) {
    nightModeStartHour = 0;
    nightModeStartMin = 0;
    nightModeEndHour = 7;
    nightModeEndMin = 0;
  }

  EEPROM.get(ADR_TIMEZONE, timezone_string);
  if (timezone_string[0] == '\\0' || timezone_string[0] == 0xFF) { // Check if empty or uninitialized (0xFF)
    strcpy(timezone_string, "CET-1CEST,M3.5.0,M10.5.0/3_2"); // Default POSIX string for Europe/Amsterdam with index
    EEPROM.put(ADR_TIMEZONE, timezone_string);
    EEPROM.commit(); // Commit immediately after setting a default
  }

  klokUniek = EEPROM.read(ADR_KLOKUNIEK) == 1; // treat uninitialized (0xFF) as false
}

void setNightMode(int timeInMinutes) {
  int nightModeStartTime = (nightModeStartHour * 60 + nightModeStartMin);
  int nightModeEndTime = (nightModeEndHour * 60 + nightModeEndMin);
  if (nightModeStartTime > nightModeEndTime) {
    // over midnight
    nightMode = checkNightMode && timeInMinutes >= nightModeStartTime || timeInMinutes < nightModeEndTime;
  } else {
    nightMode = checkNightMode && timeInMinutes >= nightModeStartTime && timeInMinutes < nightModeEndTime;
  }
  if (nightMode) {
    pixels->setBrightness(nightModeBrightness);
  } else {
    pixels->setBrightness(brightness);
  }
}

/***************************************************************
 *                       Intervals
 ***************************************************************/

void pauseTimer(long* timer, unsigned long delay = 0) {
  if (!delay) {
    *timer = LONG_MAX; // infinite
  } else {
    *timer = millis() + delay;
  }
}

void resumeTimer(long* timer) {
  *timer = millis();
}

bool readTimeInterval() {
  bool interval = (long)millis() - lastReadTime > PERIOD_READTIME;
  if (interval) lastReadTime = millis();
  return interval;
}

bool checkNTPInterval() {
  bool interval = (long)millis() - lastCheckNTP > PERIOD_NTP_CHECK;
  if (interval) lastCheckNTP = millis();
  return interval;
}

bool checkUpdateInterval() {
  bool interval = (long)millis() - lastCheckUpdate > PERIOD_CHECK_UPDATE;
  if (interval) lastCheckUpdate = millis();
  return interval;
}

bool storeColorsInterval() {
  bool interval = (storeColorName || storeColorTime) && (long)millis() - lastCheckUpdate > PERIOD_STORE_COLORS;
  if (interval) lastCheckUpdate = millis();
  return interval;
}



/***************************************************************
 *                            WiFi
 ***************************************************************/
void setupWifiManager() {
  // print("resetting WiFi settings");
  // wifiManager.resetSettings();
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setHostname(klokUniek ? "klokuniek" : HOSTNAME);
  if (wifiManager.autoConnect(klokUniek ? "KlokUniek" : AP_SSID)) {
    print("Reconnected to WiFi");
    onWifiConnect();
  }
}


bool isWifiConnected() {
  return (WiFi.status() == WL_CONNECTED) && (WiFi.localIP().toString() != "0.0.0.0");
}

void checkWifiDisconnect() {
  if (!wifiManager.getConfigPortalActive()) {
    if (WiFi.status() != WL_CONNECTED) {
      print("no wifi connection..");
      if (!nightMode) {
        pixels->setPixelColor(100, pixels->Color(255, 0, 0, 0));
        pixels->show();
      }
    } else if (WiFi.localIP().toString() == "0.0.0.0") {
      // detected bug: ESP8266 - WiFi status not changing
      // https://github.com/esp8266/Arduino/issues/2593#issuecomment-323801447
      print("no IP address, reconnecting WiFi..");
      WiFi.reconnect();
    }
  }
}

void printIP() {
  print("IP address: ");
  print(WiFi.localIP().toString());
  uint8_t address = WiFi.localIP()[3];
  pauseTimer(&lastReadTime, 10000);
  matrix->clear();
  matrix->print(FONT_I, 1, 0, LEDMatrix::OTHER);
  matrix->print(FONT_P, 5, 0, LEDMatrix::OTHER);
  uint8_t x = 0;
  // Show first number when it's not 0
  if (address / 100 != 0) {
    matrix->print(FONT_NUMBERS[(address / 100)], x, 6, LEDMatrix::OTHER);
    x += 4;
  }
  // Show second number when it's not 0
  // or when first number is not 0 (for example, hide for 005, but show for 105)
  if (address / 100 != 0 || (address / 10) % 10 != 0) {
    matrix->print(FONT_NUMBERS[(address / 10) % 10], x, 6, LEDMatrix::OTHER);
    x += 4;
  }
  matrix->print(FONT_NUMBERS[address % 10], x, 6, LEDMatrix::OTHER);
  matrix->draw(true);
}

void onWifiConnect() {
  setupOTA();
  setupServer();
  logger = UDPLogger(WiFi.localIP(), IPAddress(230, 120, 10, 2), 8123);
  printIP();
  String actualPOSIX = getActualPOSIXString(timezone_string);
  configTime(actualPOSIX.c_str(), "pool.ntp.org", "time.cloudflare.com", "time.google.com");
}

/***************************************************************
 *                    Over-the-Air Updates
 ***************************************************************/
void setupOTA() {
  ArduinoOTA.setHostname(klokUniek ? "klokuniek" : HOSTNAME);
  ArduinoOTA.begin();
  FOTA = new esp8266FOTA("wordclock", VERSION, fileSystemVersion);
  FOTA->checkURL = "https://raw.githubusercontent.com/laurensV/wordclock-beta2/main/firmware/version.json";
  FOTA->onProgress(onUpdateProgress);
  FOTA->onEnd(onUpdateFinished);
  ArduinoOTA.onProgress(onUpdateProgress);
  ArduinoOTA.onEnd(onUpdateFinished);
}

void onUpdateProgress(unsigned int progress, unsigned int total) {
  print("Progress: " + String(progress / (total / 100)));
  if (!nightMode) {
    pixels->clear();
    for (int i = 0; i < progress / (total / (clockWidth * clockHeight)); i++) {
      int row = i / clockWidth;
      pixels->setPixelColor(row % 2 ? row * clockWidth + ((row + 1) * clockWidth - i - 1) : i, pixels->Color(0, 0, 255));
    }
    pixels->show();
  }
}

void onUpdateFinished() {
  if (!nightMode) {
    pixels->clear();
    for (int i = 0; i < (clockWidth * clockHeight); i++) {
      pixels->setPixelColor(i, pixels->Color(0, 255, 0));
    }
    pixels->show();
  }
  delay(200);
}

/***************************************************************
 *                       Time
 ***************************************************************/
void setupTime() {
  // Set timezone
  String actualPOSIX = getActualPOSIXString(timezone_string);
  setTZ(actualPOSIX.c_str());
  // Start the I2C interface (needed for DS3231 clock)
  Wire.begin();
  rtc.setClockMode(false);
  bool rtcSet;
  EEPROM.get(ADR_RTC_SET, rtcSet);
  if ((rtcSet && rtc.getYear())) {
    print("RTC already set, skipping set time");
  } else {
    if (rtcSet) {
      print("WARNING: RTC lost power. Compile again or connect the WiFi to sync the time");
    }
    DateTime compiled = DateTime(__DATE__, __TIME__);
    time_t local_compile = compiled.unixtime();
    struct tm* tmptime;
    tmptime = localtime(&local_compile);
    tmptime->tm_isdst = 0;
    int32_t offset = mktime(tmptime) - mktime(gmtime(&local_compile));
    print("setting RTC to compile UTC time + 33 seconds");
    rtc.setEpoch(local_compile - offset + 33);
    EEPROM.put(ADR_RTC_SET, true);
    EEPROM.commit();
  }

  // callback for set time updates
  settimeofday_cb(time_set);
}

void showName() {
  for (int i = 0; i < clockLayout.length(); i++) {
    if (clockLayout.charAt(i) == '*') {
      int row = i / clockWidth;
      int col = i % clockWidth;
      matrix->setPixelType(row, col, LEDMatrix::NAME);
    } else if (clockLayout.charAt(i) == '&') {
      int row = i / clockWidth;
      int col = i % clockWidth;
      matrix->setPixelType(row, col, LEDMatrix::ICON);
    }
  }
}

void showDigitalTime(int hours, int minutes) {
  matrix->clear();
  matrix->print(FONT_NUMBERS[(hours / 10)], 3, 0, LEDMatrix::TIME);
  matrix->print(FONT_NUMBERS[(hours % 10)], 7, 0, LEDMatrix::TIME);
  matrix->print(FONT_NUMBERS[(minutes / 10)], 3, 6, LEDMatrix::TIME);
  matrix->print(FONT_NUMBERS[(minutes % 10)], 7, 6, LEDMatrix::TIME);
  showName();
  matrix->draw(true);
}

void showTimeString(String timeString) {
  int messageStart = 0;
  String word = "";
  int lastLetterClock = 0;
  int positionOfWord = 0;
  int nextSpace = 0;
  int index = 0;

  // add space on the end of timeString for splitting
  timeString = timeString + " ";

  matrix->clear();

  while (true) {
    // extract next word from timeString
    word = split(timeString, ' ', index);
    index++;

    if (word.length() > 0) {
      positionOfWord = clockLayout.indexOf(word, lastLetterClock);

      if (positionOfWord >= 0) {
        // word found on clock
        for (int i = 0; i < word.length(); i++) {
          int row = (positionOfWord + i) / clockWidth;
          int col = (positionOfWord + i) % clockWidth;
          matrix->setPixelType(row, col, LEDMatrix::TIME);
        }
        lastLetterClock = positionOfWord + word.length();
      } else {
        // word is not possible to show on clock
        print("word is not possible to show on clock: " + String(word));
      }
    } else {
      // end - no more word in message
      break;
    }
  }
  showName();
  matrix->draw(mode != RAINBOW);
}

/**
 * @brief Converts the given time as sentence in Dutch (String)
 *
 * @param hours hours of the time value
 * @param minutes minutes of the time value
 * @return String time as sentence
 */
String timeToStringDutch(uint8_t hours, uint8_t minutes) {
  String message = "HET IS ";

  String minuteWords[] = { "EEN", "TWEE", "DRIE", "VIER", "VIJF", "ZES", "ZEVEN", "ACHT", "NEGEN", "TIEN", "ELF", "TWAALF", "DERTIEN", "VEERTIEN", "KWART", "ZES TIEN", "ZEVEN TIEN", "ACHT TIEN", "NEGEN TIEN" };
  //show minutes
  if (minutes > 0 && minutes < 20) {
    message += minuteWords[minutes - 1] + " OVER ";
  } else if (minutes >= 20 && minutes < 30) {
    message += minuteWords[29 - minutes] + " VOOR HALF ";
  } else if (minutes == 30) {
    message += "HALF ";
  } else if (minutes > 30 && minutes < 45) {
    message += minuteWords[minutes - 31] + " OVER HALF ";
  } else if (minutes >= 45 && minutes <= 59) {
    message += minuteWords[59 - minutes] + " VOOR ";
  }

  //convert hours to 12h format
  if (hours >= 12) hours -= 12;
  if (minutes >= 20) hours++;
  if (hours == 0) hours = 12;
  message += minuteWords[hours - 1] + " ";

  if (minutes == 0) message += "UUR ";

  return message;
}

/**
 * @brief Converts the given time as sentence in English (String)
 *
 * @param hours hours of the time value
 * @param minutes minutes of the time value
 * @return String time as sentence
 */
String timeToStringEnglish(uint8_t hours, uint8_t minutes) {
  String message = "IT IS ";

  String minuteWords[] = { "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE", "THIR TEEN", "FOUR TEEN", "A QUARTER", "SIX TEEN", "SEVEN TEEN", "EIGHTEEN", "NINE TEEN", "TWENTY", "TWENTY ONE", "TWENTY TWO", "TWENTY THREE", "TWENTY FOUR", "TWENTY FIVE", "TWENTY SIX", "TWENTY SEVEN", "TWENTY EIGHT", "TWENTY NINE", "HALF" };
  // show minutes
  if (minutes > 0 && minutes <= 30) {
    message += minuteWords[minutes - 1] + " PAST ";
  } else if (minutes > 30 && minutes <= 59) {
    message += minuteWords[59 - minutes] + " TO ";
  }

  String AMPM = hours >= 12 ? "PM" : "AM";
  // convert hours to 12h format
  if (hours >= 12) hours -= 12;
  if (minutes > 30) hours++;
  if (hours == 0) hours = 12;
  message += minuteWords[hours - 1] + " ";

  if (minutes == 0) message += "OCLOCK ";
  message += AMPM;
  return message;
}

// NTP functions
void time_set(bool from_sntp) {
  if (from_sntp) {
    NTPWorking = true;
    print("System time updated from NTP server");
    // Update RTC if time came from NTP server
    rtc.setEpoch(time(nullptr));
  } else {
    print("System time updated from RTC");
  }
}
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() {
  return PERIOD_NTP_UPDATE;
}
uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000() {
  return 2000UL; // 2s, 60s limit is not a restriction anymore
}

/***************************************************************
 *                       Webserver
 ***************************************************************/
void setupServer() {
  server.enableCORS(true);
  server.onNotFound([]() {
    if (!handleFile(server.urlDecode(server.uri())))
      server.send(404, "text/plain", "FileNotFound");
  });
  server.on("/api/mode", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });
  server.on("/api/brightness", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });
  server.on("/api/color", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });
  server.on("/api/settings", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });
  server.on("/api/admin/settings", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });
  server.on("/api/settings", HTTP_GET, getSettings);
  server.on("/api/settings", HTTP_POST, saveSettings);
  server.on("/api/admin/settings", HTTP_POST, saveAdminSettings);
  server.on("/api/admin/reset-wifi", HTTP_GET, resetWifi);
  server.on("/api/admin/reset-settings", HTTP_GET, resetSettings);

  server.on("/api/color", HTTP_POST, setColor);
  server.on("/api/brightness", HTTP_POST, setBrightness);
  server.on("/api/color", HTTP_GET, getColor);
  server.on("/api/brightness", HTTP_GET, getBrightness);
  server.on("/api/mode", HTTP_POST, setMode);
  server.begin();
}

void resetWifi() {
  wifiManager.resetSettings();
  server.send(200, "application/json", "\"OK\"");
  delay(1000);
  ESP.restart();
}
void resetSettings() {
  resetEEPROM();
  server.send(200, "application/json", "\"OK\"");
  delay(1000);
  ESP.restart();
}

bool getSettings() {
  server.send(200, "application/json", "{\"version\": " + String(VERSION) + ", \"fsversion\": " + String(fileSystemVersion) + ", \"mode\": " + String(mode) + ",\"brightness\": " + String(brightness) + ",\"nm_brightness\": " + String(nightModeBrightness) + ", \"nm\": " + String(checkNightMode) + ", \"nm_start_h\": " + String(nightModeStartHour) + ", \"nm_start_m\": " + String(nightModeStartMin) + ", \"nm_end_h\": " + String(nightModeEndHour) + ", \"nm_end_m\": " + String(nightModeEndMin) + ", \"clock_width\": " + String(clockWidth) + ", \"clock_height\": " + String(clockHeight) + ", \"clock_layout\": \"" + clockLayout + "\", \"timezone\": \"" + String(timezone_string) + "\", \"klokuniek\": " + String(klokUniek) + "}");
  return true;
}

bool setMode() {
  server.sendHeader("Access-Control-Allow-Headers", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "Body not received");
    return false;
  }

  String newMode = server.arg("plain");
  print("Changing to mode " + newMode);
  if (newMode == "word_clock") {
    mode = WORD_CLOCK;
  } else if (newMode == "digital_clock") {
    mode = DIGITAL_CLOCK;
  } else if (newMode == "rainbow") {
    mode = RAINBOW;
  }
  EEPROM.put(ADR_MODE, mode);
  EEPROM.commit();

  server.send(200, "application/json", String(mode));
  return true;
}

bool setColor() {
  server.sendHeader("Access-Control-Allow-Headers", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "Body not received");
    return false;
  }
  StaticJsonDocument<300> JSONDocument;
  DeserializationError err = deserializeJson(JSONDocument, server.arg("plain"));
  if (err) { //Check for errors in parsing
    server.send(400, "application/json", "Body could not be parsed");
    return false;
  }
  uint32_t color = Adafruit_NeoPixel::Color(JSONDocument["r"], JSONDocument["g"], JSONDocument["b"]);

  if (server.arg("type") == "time") {
    color_TIME = color;
    storeColorTime = true;
  } else if (server.arg("type") == "name") {
    color_NAME = color;
    storeColorName = true;
  } else if (server.arg("type") == "icon") {
    color_ICON = color;
    storeColorIcon = true;
  } else {
    server.send(400, "application/json", "Color type unknown");
    return false;
  }
  matrix->draw(true);

  server.send(200, "application/json", server.arg("plain"));
  return true;
}

bool setBrightness() {
  server.sendHeader("Access-Control-Allow-Headers", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "Body not received");
    return false;
  }
  uint8_t b = server.arg("plain").toInt();
  if (b < 0 && b > 255) {
    server.send(400, "application/json", "Invalid brightness value");
    return false;
  }

  if (server.arg("type") == "global") {
    brightness = b;
    EEPROM.put(ADR_BRIGHTNESS, brightness);
  } else if (server.arg("type") == "nightmode") {
    nightModeBrightness = b;
    EEPROM.put(ADR_NM_BRIGHTNESS, nightModeBrightness);
  } else {
    server.send(400, "application/json", "Type unknown");
    return false;
  }
  if (!nightMode) {
    pixels->setBrightness(brightness);
  } else {
    pixels->setBrightness(nightModeBrightness);
  }
  EEPROM.commit();
  matrix->draw(true);
  server.send(200, "application/json", (String)b);
  return true;
}

bool getBrightness() {
  uint8_t b;
  if (server.arg("type") == "global") {
    b = brightness;
  } else if (server.arg("type") == "nightmode") {
    b = nightModeBrightness;
  } else {
    server.send(400, "application/json", "Type unknown");
    return false;
  }
  server.send(200, "application/json", (String)b);
  return true;
}

bool getColor() {
  uint32_t color;
  if (server.arg("type") == "time") {
    color = color_TIME;
  } else if (server.arg("type") == "name") {
    color = color_NAME;
  } else if (server.arg("type") == "icon") {
    color = color_ICON;
  } else {
    server.send(400, "application/json", "Color type unknown");
    return false;
  }
  server.send(200, "application/json", (String)color);
  return true;
}

bool saveAdminSettings() {
  server.sendHeader("Access-Control-Allow-Headers", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "Body not received");
    return false;
  }
  StaticJsonDocument<500> JSONDocument;
  DeserializationError err = deserializeJson(JSONDocument, server.arg("plain"));
  if (err) { //Check for errors in parsing
    server.send(400, "application/json", "Body could not be parsed");
    return false;
  }

  if (JSONDocument["clock_width"] > 0 && JSONDocument["clock_width"] <= 20 &&
    JSONDocument["clock_height"] > 0 && JSONDocument["clock_height"] <= 20) {
    clockWidth = JSONDocument["clock_width"];
    clockHeight = JSONDocument["clock_height"];
    print("setting clock width to ", false);print(clockWidth);
    print("setting clock height to ", false);print(clockHeight);
    EEPROM.put(ADR_CLOCK_WIDTH, clockWidth);
    EEPROM.put(ADR_CLOCK_HEIGHT, clockHeight);
    EEPROM.commit();
    if (JSONDocument["clock_layout"]) {
      clockLayout = String(JSONDocument["clock_layout"]);
      print("setting clock layout to " + clockLayout);
      for (int i = 0; i < clockLayout.length(); ++i) {
        EEPROM.write(ADR_CLOCK_LAYOUT + i, clockLayout.charAt(i));
      }
    }
    EEPROM.commit();
    setupPixels(false);
  }

  if (JSONDocument.containsKey("klokuniek")) {
    klokUniek = JSONDocument["klokuniek"];
    print("setting klokuniek to ", false);print(klokUniek);
    EEPROM.put(ADR_KLOKUNIEK, klokUniek);
    EEPROM.commit();
  }

  server.send(200, "application/json", server.arg("plain"));
  return true;
}

bool saveSettings() {
  server.sendHeader("Access-Control-Allow-Headers", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "Body not received");
    return false;
  }
  StaticJsonDocument<500> JSONDocument;
  DeserializationError err = deserializeJson(JSONDocument, server.arg("plain"));
  if (err) { //Check for errors in parsing
    server.send(400, "application/json", "Body could not be parsed");
    return false;
  }

  if (JSONDocument.containsKey("timezone")) {
    const char* tz_from_client = JSONDocument["timezone"];
    strncpy(timezone_string, tz_from_client, sizeof(timezone_string) - 1);
    timezone_string[sizeof(timezone_string) - 1] = '\\0'; // Ensure null termination
    EEPROM.put(ADR_TIMEZONE, timezone_string);
    // Update tz runtime
    String actualPOSIX = getActualPOSIXString(timezone_string);
    setTZ(actualPOSIX.c_str());
    configTime(actualPOSIX.c_str(), "pool.ntp.org", "time.cloudflare.com", "time.google.com"); // Re-config time with new TZ
  }

  checkNightMode = JSONDocument["nm"];
  EEPROM.put(ADR_NM, checkNightMode);

  if (JSONDocument["nm_start_h"] >= 0 && JSONDocument["nm_start_h"] <= 23 &&
    JSONDocument["nm_start_m"] >= 0 && JSONDocument["nm_start_m"] <= 59 &&
    JSONDocument["nm_end_h"] >= 0 && JSONDocument["nm_end_h"] <= 23 &&
    JSONDocument["nm_end_m"] >= 0 && JSONDocument["nm_end_m"] <= 59) {
    nightModeStartHour = JSONDocument["nm_start_h"];
    nightModeStartMin = JSONDocument["nm_start_m"];
    nightModeEndHour = JSONDocument["nm_end_h"];
    nightModeEndMin = JSONDocument["nm_end_m"];
    EEPROM.put(ADR_NM_START_H, nightModeStartHour);
    EEPROM.put(ADR_NM_START_M, nightModeStartMin);
    EEPROM.put(ADR_NM_END_H, nightModeEndHour);
    EEPROM.put(ADR_NM_END_M, nightModeEndMin);
  }
  EEPROM.commit();
  matrix->draw(true);
  server.send(200, "application/json", server.arg("plain"));
  return true;
}

bool handleFile(String&& path) {
  if (path.endsWith("/")) path += "index.html";
  return LittleFS.exists(path) ? ({ File f = LittleFS.open(path, "r"); server.streamFile(f, mime::getContentType(path)); f.close(); true; }) : false;
}

/***************************************************************
 *                       Helpers/Other
 ***************************************************************/
void resetEEPROM() {
  print("empty entire EEPROM " + String(EEPROM.length()));
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

/**
 * @brief Splits a string at given character and return specified element
 *
 * @param s string to split
 * @param parser separating character
 * @param index index of the element to return
 * @return String
 */
String split(String s, char parser, int index) {
  String rs = "";
  int parserIndex = index;
  int parserCnt = 0;
  int rFromIndex = 0, rToIndex = -1;
  while (index >= parserCnt) {
    rFromIndex = rToIndex + 1;
    rToIndex = s.indexOf(parser, rFromIndex);
    if (index == parserCnt) {
      if (rToIndex == 0 || rToIndex == -1) return "";
      return s.substring(rFromIndex, rToIndex);
    } else parserCnt++;
  }
  return rs;
}

void print(String message, bool newline) {
  if (newline) {
    Serial.println(message);
  } else {
    Serial.print(message);
  }
  logger.log(message);
}
void print(int number, bool newline) {
  print(String(number), newline);
}