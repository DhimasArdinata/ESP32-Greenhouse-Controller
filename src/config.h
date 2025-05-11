/**
 * @file config.h
 * @brief Global configuration file for the ESP32 T-Call Relay Controller project.
 *
 * This file consolidates all major compile-time configurations, including:
 * - Modem type definition for TinyGSM library.
 * - Debug level and macros for conditional serial output.
 * - Hardware pin assignments for ESP32 GPIOs connected to peripherals like SIM800L, SD card, relays, and I2C devices.
 * - Default network credentials (WiFi, GPRS) and API settings (base URLs, authentication token).
 *   These serve as fallbacks if no configuration is found in NVS or after a factory reset.
 * - Firmware identification (name, version) based on a default Greenhouse ID.
 * - Timing parameters for various operations: main loop, API communication, RTC sync, fail-safes, retries, and GPRS FSM states.
 * - Buffer sizes for network communication, string storage, and JSON document parsing.
 * - Settings for the web configuration portal.
 * - Miscellaneous system thresholds like RTC drift and watchdog timeout.
 *
 * @note Many settings, especially network credentials and API details, are firmware defaults
 *       and are intended to be overridden by values stored in Non-Volatile Storage (NVS)
 *       via the `DeviceConfig` class and the web configuration portal.
 * @warning **IMPORTANT**: Review and update default credentials, API endpoints, and hardware pin
 *          assignments to match your specific environment and board setup before deployment.
 */

// --- Modem Definition (MUST BE DEFINED BEFORE ANY TinyGSM library includes) ---
/**
 * @def TINY_GSM_MODEM_SIM800
 * @brief Specifies to the TinyGSM library that a SIM800 series modem (e.g., SIM800L) is being used.
 * This enables the library to use AT commands and features specific to this modem family.
 */
#define TINY_GSM_MODEM_SIM800

// --- Optional TinyGSM Configurations (Typically set in platformio.ini build_flags for project-wide effect) ---
// #define TINY_GSM_MODEM_HAS_SSL      // Enable if your SIM800L and TinyGSM library version support SSL/TLS for HTTPS.
                                      // Often requires specific library handling or build flags (e.g., -DTINY_GSM_MODEM_HAS_SSL in platformio.ini).
// #define TINY_GSM_DEBUG Serial       // Directs TinyGSM library's debug output to the main Serial port.
                                      // `Serial.begin()` must be called first. Can also be set via build flags.

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h> // For core Arduino types like `byte`, `String`, etc.

/**
 * @defgroup DebugConfig Debug Configuration
 * @brief Controls the verbosity of debug messages and provides macros for printing.
 * @{
 */

/**
 * @brief Defines the level of debug messages printed to the Serial monitor.
 * - 0: None (no debug messages).
 * - 1: Error (critical issues only).
 * - 2: Warn (potential problems or non-critical errors).
 * - 3: Info (general operational messages, default).
 * - 4: Detail (verbose output for deep debugging).
 * Higher levels include messages from all lower levels.
 */
#define DEBUG_LEVEL 3

// --- Debug Macros ---
// These macros facilitate conditional printing of debug messages based on `DEBUG_LEVEL`.
// Output is directed to the primary `Serial` interface. `Serial.begin()` must be called in `setup()`.
#if defined(DEBUG_LEVEL) && DEBUG_LEVEL > 0
    /**
     * @brief Prints formatted debug messages without a trailing newline if `DEBUG_LEVEL` is sufficient.
     * @param level The debug level required for this message to be printed.
     * @param ...   Variable arguments similar to `printf`.
     */
    #define DEBUG_PRINT(level, ...) do { if (DEBUG_LEVEL >= level) { Serial.printf(__VA_ARGS__); } } while (0)
    /**
     * @brief Prints formatted debug messages with a trailing newline if `DEBUG_LEVEL` is sufficient.
     * @param level The debug level required for this message to be printed.
     * @param ...   Variable arguments similar to `printf`.
     */
    #define DEBUG_PRINTLN(level, ...) do { if (DEBUG_LEVEL >= level) { Serial.printf(__VA_ARGS__); Serial.println(); } } while (0)
    /**
     * @brief (Compatibility) Prints formatted debug messages without a trailing newline. Same as `DEBUG_PRINT`.
     */
    #define DEBUG_PRINTF(level, ...) do { if (DEBUG_LEVEL >= level) { Serial.printf(__VA_ARGS__); } } while (0)

    /**
     * @brief Prints an F-string (PROGMEM string) without a newline if `DEBUG_LEVEL` is sufficient. Saves RAM.
     * @param level The debug level required for this message to be printed.
     * @param f_string The F-string (e.g., `F("My message")`) to print.
     */
    #define DEBUG_PRINT_F(level, f_string) do { if (DEBUG_LEVEL >= level) { Serial.print(f_string); } } while (0)
    /**
     * @brief Prints an F-string (PROGMEM string) with a newline if `DEBUG_LEVEL` is sufficient. Saves RAM.
     * @param level The debug level required for this message to be printed.
     * @param f_string The F-string (e.g., `F("My message")`) to print.
     */
    #define DEBUG_PRINTLN_F(level, f_string) do { if (DEBUG_LEVEL >= level) { Serial.println(f_string); } } while (0)
#else // DEBUG_LEVEL is 0 or undefined
    #define DEBUG_PRINT(level, ...)   do {} while (0) ///< No-op if debugging is disabled or level too low.
    #define DEBUG_PRINTLN(level, ...) do {} while (0) ///< No-op if debugging is disabled or level too low.
    #define DEBUG_PRINTF(level, ...)  do {} while (0) ///< No-op if debugging is disabled or level too low.
    #define DEBUG_PRINT_F(level, f_string)   do {} while (0) ///< No-op if debugging is disabled or level too low.
    #define DEBUG_PRINTLN_F(level, f_string) do {} while (0) ///< No-op if debugging is disabled or level too low.
#endif
/** @} */ // end of DebugConfig group


/**
 * @defgroup HardwarePins Hardware Pin Definitions
 * @brief Defines ESP32 GPIO pins connected to various hardware components.
 * @warning Verify these against your specific TTGO T-Call board version and custom wiring.
 * @{
 */

// --- SIM800L Modem Pins (TTGO T-Call V1.3/V1.4 specific) ---
// `Serial1` (pins 16/17 by default on some ESP32s, but T-Call often remaps) is typically used for hardware serial communication.
// However, for T-Call, specific GPIOs are routed to the modem. These definitions reflect that.
const int GSM_TX = 26;           ///< ESP32 TX pin to SIM800L RX. Used with a HardwareSerial instance (e.g., `Serial1` or `Serial2` if remapped).
const int GSM_RX = 27;           ///< ESP32 RX pin to SIM800L TX. Used with a HardwareSerial instance.
const int GSM_PWR = 4;           ///< ESP32 pin connected to SIM800L PWKEY (Power Key) for software power on/off.
const int GSM_RST = 5;           ///< ESP32 pin connected to SIM800L RESET pin for hardware reset.
const int MODEM_POWER_ON = 23;   ///< ESP32 pin controlling power supply to SIM800L (e.g., via MOSFET on T-Call boards).

// --- SD Card (SPI Interface) ---
// Uses VSPI peripheral by default on many ESP32 configurations.
const int SD_CS = 2;    ///< Chip Select (CS) pin for SD Card. Choose an available GPIO.
const int SD_SCK = 18;  ///< SPI Clock (SCK) pin (typically VSPI_SCLK).
const int SD_MISO = 19; ///< SPI Master In Slave Out (MISO) pin (typically VSPI_MISO).
const int SD_MOSI = 13; ///< SPI Master Out Slave In (MOSI) pin (typically VSPI_MOSI).
                        ///< @note Some T-Call boards might use GPIO23 for MOSI. Verify your board version. GPIO13 is common on V1.3/1.4.

// --- Relay Pins ---
// Ensure these are free GPIOs on your T-Call and match your relay module wiring.
// Relay logic (active HIGH or LOW) depends on the specific relay module.
const int RELAY_CH1 = 32; ///< GPIO for Relay Channel 1 (e.g., Exhaust Fan).
const int RELAY_CH2 = 33; ///< GPIO for Relay Channel 2 (e.g., Dehumidifier).
const int RELAY_CH3 = 14; ///< GPIO for Relay Channel 3 (e.g., Blower Fan).
const int RELAY_CH4 = 12; ///< GPIO for Relay Channel 4 (e.g., Unused or other actuator).

// --- I2C Pins ---
// Used for peripherals like LCD Display and DS3231 RTC Module.
const int SDA_PIN = 21; ///< Standard I2C Data line (SDA).
const int SCL_PIN = 22; ///< Standard I2C Clock line (SCL).

// --- LCD I2C Address ---
const byte LCD_ADDR = 0x27; ///< Common I2C address for 16x2 or 20x4 LCDs with PCF8574 I2C backpack.
                            ///< Verify with an I2C scanner if unsure.
/** @} */ // end of HardwarePins group


/**
 * @defgroup DefaultConfig Default Network Credentials & API Configuration
 * @brief Firmware default values for network settings and API interaction.
 * These are used if no settings are found in NVS or after a factory reset.
 * They are typically overridden by user configuration via the Web Portal.
 * @{
 */

/**
 * @brief Compile-time default Greenhouse ID (GH_ID).
 * This determines which set of other firmware defaults (e.g., WiFi SSIDs, API base URLs) are used
 * if NVS is empty or after a reset. The runtime `gh_id` is loaded from NVS by `DeviceConfig`.
 * Valid values: 1 or 2.
 */
#define GH_ID_FIRMWARE_DEFAULT 1

// --- Conditional Firmware Name & Version (Stored in PROGMEM) ---
// These identify the firmware build, primarily for display or logging.
#if GH_ID_FIRMWARE_DEFAULT == 1
    const char FW_NAME[] PROGMEM = "GH1_FW";       ///< Firmware name for Greenhouse 1 default build.
    const char FW_VERSION[] PROGMEM = "1.3.0_GH1"; ///< Firmware version for Greenhouse 1 default build.
#elif GH_ID_FIRMWARE_DEFAULT == 2
    const char FW_NAME[] PROGMEM = "GH2_FW";       ///< Firmware name for Greenhouse 2 default build.
    const char FW_VERSION[] PROGMEM = "1.3.0_GH2"; ///< Firmware version for Greenhouse 2 default build.
#else
    #error "Invalid GH_ID_FIRMWARE_DEFAULT defined in config.h. Must be 1 or 2."
#endif

// --- WiFi Defaults (Stored in PROGMEM) ---
// Fallback WiFi credentials. `DeviceConfig.cpp` selects GH1 or GH2 set based on runtime `gh_id`.
// **IMPORTANT**: Replace these placeholders with sensible defaults for your environment or initial provisioning, or configure via Web Portal.
const char DEFAULT_WIFI_SSID_GH1[] PROGMEM = "YOUR_WIFI_SSID_GH1"; ///< Default WiFi SSID for Greenhouse 1. FIXME: Replace or configure via Web Portal.
const char DEFAULT_WIFI_PWD_GH1[] PROGMEM = "YOUR_WIFI_PASSWORD_GH1";      ///< Default WiFi Password for Greenhouse 1. FIXME: Replace or configure via Web Portal.
const char DEFAULT_WIFI_SSID_GH2[] PROGMEM = "YOUR_WIFI_SSID_GH2"; ///< Default WiFi SSID for Greenhouse 2. FIXME: Replace or configure via Web Portal.
const char DEFAULT_WIFI_PWD_GH2[] PROGMEM = "YOUR_WIFI_PASSWORD_GH2";      ///< Default WiFi Password for Greenhouse 2. FIXME: Replace or configure via Web Portal.

// --- GPRS Defaults (Stored in PROGMEM) ---
// **IMPORTANT**: `GPRS_APN` is specific to your SIM card provider. Replace placeholder or configure via Web Portal.
const char GPRS_APN[] PROGMEM = "YOUR_GPRS_APN";      ///< GPRS Access Point Name. FIXME: Replace with your SIM provider's APN or configure via Web Portal.
const char GPRS_USER[] PROGMEM = "YOUR_GPRS_USER";             ///< GPRS Username (often blank). FIXME: Replace if needed or configure via Web Portal.
const char GPRS_PASSWORD[] PROGMEM = "YOUR_GPRS_PASSWORD";         ///< GPRS Password (often blank). FIXME: Replace if needed or configure via Web Portal.
const char SIM_PIN[] PROGMEM = "YOUR_SIM_PIN";               ///< SIM card PIN. Leave blank if SIM has no PIN lock. FIXME: Replace if SIM has PIN or configure via Web Portal.

// --- GPRS Modem Serial Communication Configuration ---
const long GPRS_SERIAL_BAUD_RATE = 115200; ///< Baud rate for HardwareSerial communication with GPRS Modem (e.g., SIM800L).
                                           ///< Standard configuration is `SERIAL_8N1` (8 data bits, no parity, 1 stop bit).

// --- API Endpoint Base URLs (Stored in PROGMEM) ---
// Base URLs for API communication. `DeviceConfig::constructApiUrls()` appends `?gh_id=<runtime_gh_id>`.
// Compile-time selection based on `GH_ID_FIRMWARE_DEFAULT`. Runtime URLs are in `DeviceConfig`.
// **IMPORTANT**: Replace placeholder URLs with your actual backend API servers or configure via Web Portal.
#if GH_ID_FIRMWARE_DEFAULT == 1
    const char DEFAULT_API_THD_BASE_URL[] PROGMEM = "YOUR_API_THD_BASE_URL_GH1";                     ///< Default base URL for Temp/Humidity/Light data API (GH1). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_AVG_SENSOR_BASE_URL[] PROGMEM = "YOUR_API_AVG_SENSOR_BASE_URL_GH1"; ///< Default base URL for Nutrient/Avg Sensor data API (GH1). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_STATUS_GET_BASE_URL[] PROGMEM = "YOUR_API_STATUS_GET_BASE_URL_GH1"; ///< Default base URL for GETting device status/commands (GH1). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_STATUS_POST_BASE_URL[] PROGMEM = "YOUR_API_STATUS_POST_BASE_URL_GH1";   ///< Default base URL for POSTing device status (GH1). FIXME: Replace or configure via Web Portal.
#elif GH_ID_FIRMWARE_DEFAULT == 2
    // Define different base URLs for GH2 if needed, otherwise they can be same as GH1.
    const char DEFAULT_API_THD_BASE_URL[] PROGMEM = "YOUR_API_THD_BASE_URL_GH2";                     ///< Default base URL for Temp/Humidity/Light data API (GH2, if different). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_AVG_SENSOR_BASE_URL[] PROGMEM = "YOUR_API_AVG_SENSOR_BASE_URL_GH2"; ///< Default base URL for Nutrient/Avg Sensor data API (GH2, if different). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_STATUS_GET_BASE_URL[] PROGMEM = "YOUR_API_STATUS_GET_BASE_URL_GH2"; ///< Default base URL for GETting device status/commands (GH2, if different). FIXME: Replace or configure via Web Portal.
    const char DEFAULT_API_STATUS_POST_BASE_URL[] PROGMEM = "YOUR_API_STATUS_POST_BASE_URL_GH2";   ///< Default base URL for POSTing device status (GH2, if different). FIXME: Replace or configure via Web Portal.
#else
    #error "Invalid GH_ID_FIRMWARE_DEFAULT defined for API URLs. Must be 1 or 2."
#endif

// --- World Time API URL (Stored in PROGMEM) ---
const char WORLDTIME_URL[] PROGMEM = "YOUR_WORLDTIME_API_URL"; ///< Placeholder for World Time API URL (e.g., http://worldtimeapi.org/api/timezone/Asia/Jakarta). **Replace or configure via Web Portal, ensure correct timezone.**

// --- API Authentication Default (Stored in PROGMEM) ---
// Fallback API token if not configured in NVS.
// **IMPORTANT**: Replace placeholder with your actual default API token or configure via Web Portal.
const char AUTH[] PROGMEM = "YOUR_API_TOKEN"; ///< Default API Authentication Token/Key. FIXME: Replace or configure via Web Portal.
/** @} */ // end of DefaultConfig group


/**
 * @defgroup TimingConfig Timing Configuration
 * @brief Constants controlling various operational timings, intervals, and timeouts (all in milliseconds unless noted).
 * @{
 */
const unsigned long LOOP_MS = 5000;                                 ///< Main control loop cycle duration (e.g., sensor reading, display update). (5 seconds)
const unsigned long API_MS = 15000;                                 ///< Interval for attempting API data fetch/send. (15 seconds)
const unsigned long TIME_SYNC_INTERVAL = 24 * 3600 * 1000UL;        ///< How often to synchronize RTC with network time. (24 hours)
const unsigned long STALE_DATA_THRESHOLD_MS = 30 * 60 * 1000UL;     ///< Duration after which fetched sensor data is considered stale. (30 minutes)
const unsigned long FAILSAFE_TIMEOUT_MS = 2 * 60 * 60 * 1000UL;     ///< Duration of network/API unavailability before entering FAILSAFE mode. (2 hours)
const unsigned long SD_RETRY_INTERVAL_MS = 5 * 60 * 1000UL;         ///< Interval to retry SD card initialization if it fails. (5 minutes)
const unsigned long INITIAL_RETRY_DELAY_MS = 15 * 1000UL;           ///< Initial delay for general connection retries (WiFi, API) with exponential backoff. (15 seconds)
const unsigned long MAX_RETRY_DELAY_MS = 5 * 60 * 1000UL;           ///< Maximum delay for exponential backoff connection retries. (5 minutes)
const unsigned long WIFI_RETRY_WHEN_GPRS_MS = 15 * 60 * 1000UL;     ///< Initial delay to attempt switching back to WiFi when on GPRS failover. (15 minutes)
const unsigned long MAX_WIFI_RETRY_WHEN_GPRS_MS = 60 * 60 * 1000UL; ///< Max backoff delay for attempting to switch back to WiFi when on GPRS. (60 minutes)
const unsigned long MANUAL_OVERRIDE_DURATION_MS = 30 * 1000UL;      ///< Duration for a manual relay override command from API. (30 seconds)
const unsigned long DEVICE_STATUS_CHECK_INTERVAL_MS = 10 * 1000UL;  ///< How often to poll API for new device status/commands. (10 seconds)

/** @defgroup GPRSTiming GPRS Finite State Machine (FSM) Timing and Retry Configuration
 *  @ingroup TimingConfig
 *  @{
 */
const unsigned long GPRS_MODEM_RESPONSE_TIMEOUT_MS = 10000UL;      ///< Max time to wait for common AT command responses from modem. (10s)
const unsigned long GPRS_APN_SET_TIMEOUT_MS = 30000UL;             ///< Max time for APN setting sequence. (30s)
const unsigned long GPRS_ATTACH_TIMEOUT_MS = 60000UL;              ///< Max time to wait for GPRS network attachment. (60s)
const unsigned long GPRS_TCP_CONNECT_TIMEOUT_MS = 60000UL;         ///< Max time for a GPRS TCP connection attempt. (60s)
const unsigned long GPRS_MODEM_RESET_PULSE_MS = 200UL;             ///< Duration of pulse on modem's reset pin. (200ms)
const unsigned long GPRS_MODEM_POWER_CYCLE_DELAY_MS = 5000UL;      ///< Delay after power-cycling modem before re-initialization. (5s)
const unsigned long GPRS_RECONNECT_DELAY_INITIAL_MS = 15 * 1000UL; ///< Initial delay for GPRS reconnection attempts after failure. (15s)
const unsigned long GPRS_RECONNECT_DELAY_MAX_MS = 10 * 60 * 1000UL;///< Max delay for GPRS reconnection attempts using backoff. (10 minutes)

// --- GPRS FSM Failure Thresholds (Counts) ---
const uint8_t GPRS_MAX_MODEM_RESETS = 3;                           ///< Max consecutive modem hardware/software resets before critical failure state.
const uint8_t GPRS_MAX_ATTACH_FAILURES = 5;                        ///< Max consecutive GPRS network attach failures before modem reset.
const uint8_t GPRS_APN_SET_RETRY_LIMIT = 3;                        ///< Max retries for setting APN before modem reset.

// --- Additional GPRS FSM Operational Timings ---
const unsigned long GPRS_CONNECTION_CHECK_INTERVAL_MS = 30000UL;    ///< How often to proactively check GPRS connection in OPERATIONAL state. (30s)
const uint8_t GPRS_MAX_RECONNECT_ATTEMPTS = 5;                      ///< Max GPRS reconnection attempts before modem restart sequence.
const unsigned long GPRS_MODEM_ERROR_RESTART_DELAY_MS = 60000UL;    ///< Delay before restarting modem after significant error. (60s)
const unsigned long GPRS_MODEM_FAIL_RECOVERY_TIMEOUT_MS = 5 * 60 * 1000UL; ///< Time in MODEM_FAIL state before full recovery attempt. (5 min)
/** @} */ // end of GPRSTiming group

/** @defgroup HTTPTiming GPRS and General HTTP Timeouts and Retries
 *  @ingroup TimingConfig
 *  @{
 */
// GPRS HTTP Specific Timeouts
const unsigned long GPRS_HTTP_TOTAL_TIMEOUT_MS = 60000UL;      ///< Overall timeout for an entire GPRS HTTP transaction. (60s)
const unsigned long GPRS_HTTP_CONNECT_TIMEOUT_MS = 20000UL;    ///< Timeout for `client.connect()` in GPRS HTTP. (20s)
const unsigned long GPRS_HTTP_HEADER_TIMEOUT_MS = 20000UL;     ///< Timeout for receiving HTTP response headers over GPRS. (20s)
const unsigned long GPRS_HTTP_BODY_TIMEOUT_MS = 30000UL;       ///< Timeout for receiving HTTP response body over GPRS. (30s)

// General HTTP Timeouts (primarily for WiFi)
const unsigned long HTTP_CONNECT_TIMEOUT_MS = 15000UL;         ///< Timeout for establishing HTTP connection (typically WiFi). (15s)
const unsigned long HTTP_RESPONSE_TIMEOUT_MS = 20000UL;        ///< Timeout for waiting for HTTP response (typically WiFi). (20s)
const unsigned long HTTP_RETRY_DELAY_MS = 5000UL;              ///< Delay before retrying a failed HTTP request. (5s)
const uint8_t MAX_HTTP_RETRIES = 3;                            ///< Maximum number of retries for a single HTTP request.
/** @} */ // end of HTTPTiming group

const unsigned long MODEM_SERIAL_WAIT_TIMEOUT_MS = 30000UL;    ///< Max time to wait for modem serial interface to become responsive during init. (30s)
/** @} */ // end of TimingConfig group


/**
 * @defgroup NetworkFeatures Network Features
 * @brief Flags to enable or disable certain network functionalities.
 * @{
 */
const bool ENABLE_GPRS_FAILOVER = true; ///< If true, system attempts GPRS if WiFi fails or is unavailable.
/** @} */ // end of NetworkFeatures group


/**
 * @defgroup BufferSizes Network & Buffer Sizes
 * @brief Defines maximum lengths for strings and buffers for network config and communication.
 * Sizes include space for a null terminator. Crucial for preventing overflows.
 * @{
 */

/** @defgroup ConfigStringLengths Configuration String Maximum Lengths
 *  @ingroup BufferSizes
 *  @brief Max lengths for configuration strings (used by `DeviceConfig.h` and NVS).
 *  @{
 */
#define WIFI_SSID_MAX_LEN 33     ///< Max 32 chars for SSID + null terminator.
#define WIFI_PWD_MAX_LEN 65      ///< Max 64 chars for WPA2 password + null terminator.
#define API_TOKEN_MAX_LEN 129    ///< Max 128 chars for API token + null terminator.
#define API_URL_MAX_LEN 257      ///< Max 256 chars for a full API URL + null terminator (generous).
#define GPRS_APN_MAX_LEN 101     ///< Max 100 chars for GPRS APN + null terminator.
#define GPRS_USER_MAX_LEN 65     ///< Max 64 chars for GPRS username + null terminator.
#define GPRS_PWD_MAX_LEN 65      ///< Max 64 chars for GPRS password + null terminator.
#define SIM_PIN_MAX_LEN 9        ///< Max 8 chars for SIM PIN + null terminator.
/** @} */ // end of ConfigStringLengths group

/** @defgroup GPRSHttpBufferSizes GPRS HTTP Request Component & Communication Buffer Sizes
 *  @ingroup BufferSizes
 *  @{
 */
// GPRS HTTP Request Component Sizes (derived from API_URL_MAX_LEN for simplicity)
#define GPRS_MAX_HOST_LEN API_URL_MAX_LEN ///< Max length for host part of URL in GPRS HTTP requests.
#define GPRS_MAX_PATH_LEN API_URL_MAX_LEN ///< Max length for path part of URL in GPRS HTTP requests.

// Network Response Buffer Size (used by NetworkFacade for its internal _apiResponse buffer, linked to GPRS_BODY_BUFFER_SIZE)
// This buffer must be large enough to hold the largest expected raw response from ANY active network interface (WiFi or GPRS) before parsing.
#define NETWORK_MAX_RESPONSE_LEN GPRS_BODY_BUFFER_SIZE ///< Max size for raw API responses.

// GPRS HTTP Communication Buffers
// **IMPORTANT**: `GPRS_BODY_BUFFER_SIZE` must hold largest expected JSON response. Truncation causes parsing failure.
#define GPRS_REQUEST_BUFFER_SIZE 512   ///< Buffer for outgoing GPRS HTTP request headers & small POST payloads.
#define GPRS_HEADER_BUFFER_SIZE 512    ///< Buffer for incoming GPRS HTTP response headers (individual lines).
#define GPRS_MAX_HEADER_SIZE 1024      ///< Max total size for all received HTTP headers combined.
#define GPRS_BODY_BUFFER_SIZE 1024     ///< Buffer for incoming GPRS HTTP response body. **Adjust based on max expected JSON payload size.**
/** @} */ // end of GPRSHttpBufferSizes group

/** @defgroup JsonDocumentSizes ArduinoJson Document Sizes
 *  @ingroup BufferSizes
 *  @brief Memory allocation for `StaticJsonDocument` or `DynamicJsonDocument`.
 *  Use ArduinoJson Assistant (arduinojson.org/v6/assistant) to calculate appropriate sizes.
 *  @{
 */
#define JSON_DOC_SIZE_DEVICE_CONFIG 1024 ///< For parsing device config/thresholds from API (e.g., `WiFiManager`, `GPRSManager`).
#define JSON_DOC_SIZE_STATUS_POST 256    ///< For creating small JSON payloads to post device status (e.g., in main `.ino`).
/** @} */ // end of JsonDocumentSizes group
/** @} */ // end of BufferSizes group


/**
 * @defgroup NVSConfig Non-Volatile Storage (NVS) Configuration
 * @brief Defines keys and namespace for storing persistent configuration data.
 * These are used by `DeviceConfig` to save/load settings like WiFi credentials,
 * API token, and Greenhouse ID.
 * @{
 */
#define NVS_NAMESPACE "device_cfg" ///< Namespace for NVS preferences.

// Current configuration keys
#define NVS_KEY_GH_ID "gh_id"          ///< Key for storing the Greenhouse ID (integer).
#define NVS_KEY_SSID "wifi_ssid"       ///< Key for storing WiFi SSID (string).
#define NVS_KEY_PWD "wifi_pwd"         ///< Key for storing WiFi password (string).
#define NVS_KEY_TOKEN "api_token"      ///< Key for storing API authentication token (string).

// Keys for deprecated/old configuration values (might be used for migration or clearing from older firmware versions).
// Consider removing these if no active migration/cleanup logic uses them.
#define NVS_KEY_OLD_TH_URL "th_url_old"     ///< Deprecated: Old user-configurable THD data URL.
#define NVS_KEY_OLD_ND_URL "nd_url_old"     ///< Deprecated: Old user-configurable Nutrient data URL.
/** @} */ // end of NVSConfig group


/**
 * @defgroup WebPortalConfig Web Configuration Portal Settings
 * @brief Settings related to the WiFiManager-based configuration portal.
 * @{
 */
const unsigned long PORTAL_TIMEOUT = 5 * 60 * 1000; ///< Timeout for web config portal to remain active (5 minutes).
/** @} */ // end of WebPortalConfig group


/**
 * @defgroup SystemThresholds Miscellaneous System Thresholds & Time Settings
 * @brief System-level thresholds and time-related settings.
 * @{
 */
const uint32_t RTC_DRIFT_THRESHOLD_SECONDS = 60;     ///< If RTC time drifts by more than this from network time, force sync (60 seconds).
const int WDT_TIMEOUT = 60;                          ///< ESP32 Watchdog Timer timeout in seconds. Main loop must reset WDT. (60 seconds)
const int NTP_TIMEZONE_OFFSET_SECONDS = 7 * 3600;    ///< Timezone offset from UTC in seconds (e.g., GMT+7 for Jakarta). Used for local time.
/** @} */ // end of SystemThresholds group


#endif // CONFIG_H
