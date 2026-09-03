#ifndef wordclock_h
#define wordclock_h

// ----------------------------------------------------------------------------------
//                                   CONSTANTS
// ----------------------------------------------------------------------------------
#define PERIOD_READTIME 1000                  // how often to read and update the time
#define PERIOD_CHECK_UPDATE 60 * 30 * 1000    // how often to check for updates
#define PERIOD_STORE_COLORS 60 * 1000         // how long to wait before storing a color in EEPROM
#define PERIOD_NTP_UPDATE 60 * 1000           // how often to sync time with NTP server
#define PERIOD_NTP_CHECK PERIOD_NTP_UPDATE*3  // check if NTP is still responding
#define AP_SSID "JouwWoordklok"               // WiFi Name when setting up clock
#define HOSTNAME "jouwwoordklok"              // Hostname of clock
#define AUTO_UPDATE true                      // automatically pull firmware and filesystem updates from remote server
#define NEOPIXEL_PIN 0                        // pin to which the NeoPixels are attached

// EEPROM to save persistent variables
#define ADR_RTC_SET       0                                             // bool
#define ADR_MODE          sizeof(bool)                                  // enum MODE
#define ADR_COLOR_TIME    (ADR_MODE + sizeof(MODE))                     // uint32_t
#define ADR_COLOR_NAME    (ADR_COLOR_TIME + sizeof(uint32_t))           // uint32_t
#define ADR_COLOR_ICON    (ADR_COLOR_NAME + sizeof(uint32_t))           // uint32_t
#define ADR_BRIGHTNESS    (ADR_COLOR_ICON + sizeof(uint32_t))           // uint8_t
#define ADR_NM            (ADR_BRIGHTNESS + sizeof(uint8_t))            // bool
#define ADR_NM_START_H    (ADR_NM + sizeof(bool))                       // uint8_t
#define ADR_NM_END_H      (ADR_NM_START_H + sizeof(uint8_t))            // uint8_t
#define ADR_NM_START_M    (ADR_NM_END_H + sizeof(uint8_t))              // uint8_t
#define ADR_NM_END_M      (ADR_NM_START_M + sizeof(uint8_t))            // uint8_t
#define ADR_CLOCK_WIDTH   (ADR_NM_END_M + sizeof(uint8_t))              // uint8_t
#define ADR_CLOCK_HEIGHT  (ADR_CLOCK_WIDTH + sizeof(uint8_t))           // uint8_t
#define ADR_CLOCK_LAYOUT  (ADR_CLOCK_HEIGHT + sizeof(uint8_t))          // 20 * 20 char
#define ADR_NM_BRIGHTNESS (ADR_CLOCK_LAYOUT + (sizeof(char) * 20 * 20)) // uint8_t
#define ADR_TIMEZONE      (ADR_NM_BRIGHTNESS + sizeof(uint8_t))         // char[50]
#define ADR_KLOKUNIEK     (ADR_TIMEZONE + (sizeof(char) * 50))          // bool

#define EEPROM_SIZE       (ADR_KLOKUNIEK + sizeof(bool))

// ----------------------------------------------------------------------------------
//                              FUNCTIONS & VARS
// ----------------------------------------------------------------------------------
enum MODE { WORD_CLOCK, DIGITAL_CLOCK, RAINBOW };

inline MODE mode;
inline uint32_t color_TIME;
inline uint32_t color_NAME;
inline uint32_t color_ICON;
inline uint8_t brightness;
inline bool checkNightMode;
inline bool nightMode;
inline uint8_t nightModeStartHour;
inline uint8_t nightModeStartMin;
inline uint8_t nightModeEndHour;
inline uint8_t nightModeEndMin;
inline uint8_t nightModeBrightness;
inline uint8_t clockWidth;
inline uint8_t clockHeight;
inline String clockLayout;
inline char timezone_string[50];
inline bool klokUniek;

void print(String message, bool newline = true);
void print(int number, bool newline = true);

#endif