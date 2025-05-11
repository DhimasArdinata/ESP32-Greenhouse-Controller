/**
 * @file DeviceConfig.h
 * @brief Defines the `DeviceConfig` structure for managing all device runtime configuration settings.
 *
 * This file declares the `DeviceConfig` structure, which serves as a central repository for
 * all parameters that can be configured at runtime. This includes:
 * - WiFi credentials (SSID, password).
 * - GPRS settings (APN, user, password, SIM PIN) - typically firmware defaults.
 * - API interaction parameters (authentication token, various endpoint URLs).
 * - A unique device identifier (`gh_id` - Greenhouse ID).
 *
 * The `DeviceConfig` structure is responsible for:
 * 1. Loading these settings from Non-Volatile Storage (NVS) on the ESP32 during startup.
 *    It uses the ESP32 `Preferences` library for this purpose.
 * 2. If settings are not found in NVS (e.g., on first boot or after a factory reset),
 *    it applies firmware-defined default values. These defaults are typically specified
 *    in `config.h` (e.g., `DEFAULT_GH_ID`, `DEFAULT_WIFI_SSID`).
 * 3. Saving any applied defaults or newly configured settings back to NVS to ensure
 *    persistence across reboots.
 * 4. Providing methods to update the configuration (`saveConfig()`) and to revert all
 *    configurable settings to their firmware defaults (`factoryResetConfig()`).
 * 5. Dynamically constructing full API endpoint URLs based on base URLs (from `config.h`)
 *    and the current `gh_id`.
 *
 * All string buffer sizes (e.g., `WIFI_SSID_MAX_LEN`, `API_URL_MAX_LEN`) and NVS keys
 * (e.g., `NVS_KEY_GH_ID`, `NVS_NAMESPACE`) are defined in `config.h`.
 */
#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include "config.h"       // Provides compile-time constants: default values, buffer sizes, NVS keys, API base URLs.
#include <Arduino.h>      // For core Arduino types (String, etc.) and functions.
#include <Preferences.h>  // For ESP32 Non-Volatile Storage (NVS) access.

// --- NVS (Non-Volatile Storage) Constants ---
// These are defined in config.h but reiterated here for Doxygen context.
// Make sure these match the definitions in config.h.
// const char* const NVS_NAMESPACE = "device-config";
// const char* const NVS_KEY_GH_ID = "cfg_ghid";
// const char* const NVS_KEY_SSID = "cfg_ssid";
// const char* const NVS_KEY_PWD = "cfg_pwd";
// const char* const NVS_KEY_TOKEN = "cfg_token";
// const char* const NVS_KEY_OLD_TH_URL = "th_url"; // Deprecated
// const char* const NVS_KEY_OLD_ND_URL = "nd_url"; // Deprecated

/**
 * @struct DeviceConfig
 * @brief Encapsulates all runtime-configurable parameters for the device.
 *
 * This structure holds all configuration data that the device uses during its operation.
 * Upon instantiation (typically once at startup), it attempts to load these values from
 * NVS. If values are missing or a factory reset is performed, it falls back to
 * firmware defaults specified in `config.h` and then saves these defaults to NVS.
 *
 * The `gh_id` (Greenhouse ID) is a critical piece of configuration. It not only
 * identifies the device but also dictates which set of default credentials (if applicable,
 * e.g., `DEFAULT_WIFI_SSID_GH1` vs `DEFAULT_WIFI_SSID_GH2`) are used and how API
 * endpoint URLs are constructed (e.g., `API_BASE_URL_TH_DATA` + `"?gh_id="` + `gh_id`).
 */
struct DeviceConfig {
public:
    // --- WiFi Credentials (Loaded from NVS or Firmware Default) ---
    /**
     * @brief Buffer for the WiFi network's Service Set Identifier (SSID).
     * Max length: `WIFI_SSID_MAX_LEN` (from `config.h`).
     * Loaded from NVS using `NVS_KEY_SSID`. Defaults to `DEFAULT_WIFI_SSID_GH1` or `DEFAULT_WIFI_SSID_GH2`
     * based on `gh_id` if not found in NVS.
     */
    char ssid[WIFI_SSID_MAX_LEN];
    /**
     * @brief Buffer for the WiFi network's password.
     * Max length: `WIFI_PWD_MAX_LEN` (from `config.h`).
     * Loaded from NVS using `NVS_KEY_PWD`. Defaults to `DEFAULT_WIFI_PWD_GH1` or `DEFAULT_WIFI_PWD_GH2`
     * based on `gh_id` if not found in NVS.
     */
    char password[WIFI_PWD_MAX_LEN];

    // --- GPRS Credentials (Firmware Defaults Only, Not NVS Configurable) ---
    /** @brief GPRS Access Point Name. Max length: `GPRS_APN_MAX_LEN`. Set from `DEFAULT_GPRS_APN` in `config.h`. */
    char gprs_apn[GPRS_APN_MAX_LEN];
    /** @brief GPRS Username. Max length: `GPRS_USER_MAX_LEN`. Set from `DEFAULT_GPRS_USER` in `config.h`. */
    char gprs_user[GPRS_USER_MAX_LEN];
    /** @brief GPRS Password. Max length: `GPRS_PWD_MAX_LEN`. Set from `DEFAULT_GPRS_PWD` in `config.h`. */
    char gprs_password[GPRS_PWD_MAX_LEN];
    /** @brief SIM Card PIN (if required). Max length: `SIM_PIN_MAX_LEN`. Set from `DEFAULT_SIM_PIN` in `config.h`. */
    char sim_pin[SIM_PIN_MAX_LEN];

    // --- API Configuration ---
    /**
     * @brief API Authentication Token (e.g., Bearer token).
     * Max length: `API_TOKEN_MAX_LEN` (from `config.h`).
     * Loaded from NVS using `NVS_KEY_TOKEN`. Defaults to `DEFAULT_API_TOKEN` if not found in NVS.
     */
    char api_token[API_TOKEN_MAX_LEN];
    /**
     * @brief Fully constructed URL for posting Temperature, Humidity, and Light (THL) sensor data.
     * Max length: `API_URL_MAX_LEN`. Dynamically built using `API_BASE_URL_TH_DATA` (from `config.h`) and the current `gh_id`.
     * Example: `http://example.com/api/thl_data?gh_id=1`
     */
    char th_url[API_URL_MAX_LEN];
    /**
     * @brief Fully constructed URL for posting Nutrient Data (or other averaged sensor data).
     * Max length: `API_URL_MAX_LEN`. Dynamically built using `API_BASE_URL_ND_DATA` (from `config.h`) and the current `gh_id`.
     * Example: `http://example.com/api/nd_data?gh_id=1`
     */
    char nd_url[API_URL_MAX_LEN];
    /**
     * @brief URL for the World Time API service, used for time synchronization.
     * Max length: `API_URL_MAX_LEN`. Set from `WORLDTIME_API_URL` in `config.h` (firmware default).
     */
    char worldtime_url[API_URL_MAX_LEN];
    /**
     * @brief Fully constructed URL for POSTing device status updates or heartbeats.
     * Max length: `API_URL_MAX_LEN`. Built using `API_BASE_URL_DEVICE_STATUS` and `gh_id`.
     */
    char device_status_post_url[API_URL_MAX_LEN];
    /**
     * @brief Fully constructed URL for GETting device commands or remote status information.
     * Max length: `API_URL_MAX_LEN`. Built using `API_BASE_URL_DEVICE_COMMANDS` (or similar) and `gh_id`.
     */
    char device_status_get_url[API_URL_MAX_LEN];

    // --- Device Identification ---
    /**
     * @brief Greenhouse Identifier (typically 1 or 2).
     * This ID is crucial:
     * - It's loaded from NVS using `NVS_KEY_GH_ID`. Defaults to `DEFAULT_GH_ID` (from `config.h`) if not found.
     * - It determines which set of default WiFi credentials (e.g., `DEFAULT_WIFI_SSID_GH1` vs. `DEFAULT_WIFI_SSID_GH2`) are applied.
     * - It is appended as a query parameter (e.g., `?gh_id=X`) to many API URLs by `constructApiUrls()`.
     * Valid values are usually 1 or 2, as checked in `saveConfig()`.
     */
    int gh_id;

    /**
     * @brief Constructor for `DeviceConfig`.
     * Initializes a new `DeviceConfig` object. Upon construction, it immediately calls
     * `loadConfigFromNvsOrDefaults()` to populate all its member variables with values
     * read from NVS or with firmware defaults if NVS entries are missing/invalid.
     * This ensures the `DeviceConfig` object is always in a valid state after creation.
     */
    DeviceConfig();

    /**
     * @brief Saves the provided configuration settings to NVS and updates the in-memory `DeviceConfig` members.
     *
     * This method attempts to write the new `gh_id`, `ssid`, `password`, and `api_token`
     * to their respective keys in NVS under the `NVS_NAMESPACE`.
     * If the `new_gh_id` is different from the current `this->gh_id`, or if it's the initial save,
     * it calls `constructApiUrls()` to rebuild API endpoint URLs based on the new `gh_id`.
     * The GPRS settings and `worldtime_url` are not saved by this method as they are
     * considered firmware defaults.
     *
     * @param new_gh_id The new Greenhouse ID. Must be a valid ID (e.g., 1 or 2, as per `VALID_GH_IDS` in `config.h`).
     * @param new_ssid The new WiFi SSID. Its length must not exceed `WIFI_SSID_MAX_LEN - 1`.
     * @param new_password The new WiFi Password. Its length must not exceed `WIFI_PWD_MAX_LEN - 1`.
     * @param new_api_token The new API Authentication Token. Its length must not exceed `API_TOKEN_MAX_LEN - 1`.
     * @return `true` if all specified settings were successfully validated, written to NVS,
     *         and the in-memory `DeviceConfig` members were updated.
     * @return `false` if `new_gh_id` is invalid, if NVS cannot be opened (begin failed),
     *         or if any NVS write operation (`putInt`, `putString`) fails.
     */
    bool saveConfig(int new_gh_id, const char* new_ssid, const char* new_password, const char* new_api_token);

    /**
     * @brief Resets all NVS-configurable settings back to their firmware defaults.
     *
     * This function performs a "factory reset" of the device's configuration by:
     * 1. Opening the `NVS_NAMESPACE` in NVS.
     * 2. Removing (clearing) the NVS entries for `NVS_KEY_GH_ID`, `NVS_KEY_SSID`,
     *    `NVS_KEY_PWD`, and `NVS_KEY_TOKEN`.
     * 3. Also removes any deprecated NVS keys like `NVS_KEY_OLD_TH_URL` to clean up old storage.
     * 4. Closing NVS.
     * 5. Crucially, it then calls `loadConfigFromNvsOrDefaults()`. Since the NVS keys
     *    have just been cleared, `loadConfigFromNvsOrDefaults()` will not find them and will
     *    therefore apply the firmware defaults (defined in `config.h`). It will then
     *    automatically save these newly applied defaults back to NVS, effectively
     *    re-populating NVS with the factory default configuration.
     *
     * After this call, the `DeviceConfig` object will hold the firmware default values.
     */
    void factoryResetConfig();

private:
    Preferences preferences; ///< ESP32 `Preferences` object used for all NVS read/write operations.
                             ///< It's kept private to encapsulate NVS access logic within `DeviceConfig` methods.
    
    /**
     * @brief Constructs all dynamic API endpoint URLs based on base URLs and the current `gh_id`.
     *
     * This method is called internally:
     * - After `gh_id` is loaded from NVS or set to a default in `loadConfigFromNvsOrDefaults()`.
     * - After a new `gh_id` is successfully saved in `saveConfig()`.
     *
     * It takes base API URLs (e.g., `API_BASE_URL_TH_DATA`, `API_BASE_URL_ND_DATA` from `config.h`)
     * and appends the `gh_id` as a query parameter (e.g., `"?gh_id=1"`) to form the complete
     * URLs stored in `th_url`, `nd_url`, `device_status_post_url`, and `device_status_get_url`.
     * The `worldtime_url` is typically a static URL from `config.h` and is just copied.
     * Ensures all constructed URLs fit within their respective `API_URL_MAX_LEN` buffers.
     *
     * @see `DeviceConfig.cpp` for the detailed implementation of URL formatting.
     */
    void constructApiUrls();

    /**
     * @brief Loads configuration from NVS; if any setting is not found, applies firmware defaults and saves them back to NVS.
     *
     * This is the core initialization logic called by the constructor and after a `factoryResetConfig()`.
     * For each configurable item (`gh_id`, `ssid`, `password`, `api_token`):
     * 1. It attempts to read the value from NVS using its specific key (e.g., `NVS_KEY_SSID`)
     *    within the `NVS_NAMESPACE`.
     * 2. If the NVS read fails (e.g., key not found, NVS error), or if the read value is invalid
     *    (e.g., empty string for SSID when a default exists), it applies the corresponding
     *    firmware default value (from `config.h`, often dependent on the `gh_id`).
     * 3. If a default value was applied (because NVS data was missing/invalid), this method
     *    then attempts to save that default value back to NVS for persistence. This ensures
     *    that after the first boot or a reset, NVS is populated with valid settings.
     *
     * After loading/defaulting `gh_id`, `ssid`, `password`, and `api_token`, it populates
     * GPRS settings and `worldtime_url` directly from firmware defaults in `config.h`
     * (these are not NVS-configurable).
     * Finally, it calls `constructApiUrls()` to build the dynamic API endpoint URLs.
     *
     * @see `DeviceConfig.cpp` for the detailed NVS interaction and default application logic.
     */
    void loadConfigFromNvsOrDefaults();
};

#endif // DEVICE_CONFIG_H