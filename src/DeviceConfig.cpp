#include "DeviceConfig.h"
#include "config.h" // For firmware defaults like DEFAULT_API_THD_BASE_URL, GH_ID_FIRMWARE_DEFAULT etc.
#include <Preferences.h> 

// Method Implementations

/**
 * @brief Constructs all necessary API endpoint URLs.
 *
 * This method dynamically builds the full API URLs (e.g., for THD data, sensor data, device status)
 * by taking base URLs defined in `config.h` (and stored in PROGMEM) and appending the
 * current `gh_id` as a query parameter. The `worldtime_url` is an exception as it's
 * typically common and does not require a `gh_id`.
 * The resulting URLs are stored in the corresponding char arrays of the DeviceConfig instance.
 * Ensures all constructed URLs are null-terminated.
 */
void DeviceConfig::constructApiUrls() {
    // URLs are based on constants from config.h (often stored in PROGMEM to save RAM).
    // A temporary buffer is used to copy the PROGMEM string before formatting with snprintf.
    char base_url_buffer[100]; // Temporary buffer for base URLs. Assumed base URLs from config.h are < 100 chars.
                               // Final URL buffers (e.g., th_url) are larger (API_URL_MAX_LEN).

    // --- TH URL (Temperature, Humidity, Light Intensity Data) ---
    // 1. Copy the PROGMEM base URL string into the temporary buffer, ensuring null termination.
    strncpy_P(base_url_buffer, DEFAULT_API_THD_BASE_URL, sizeof(base_url_buffer) - 1);
    base_url_buffer[sizeof(base_url_buffer) - 1] = '\0'; // Ensure null termination for base_url_buffer.
    // 2. Format the final URL string, appending gh_id, into the 'th_url' member.
    snprintf(th_url, sizeof(th_url), "%s?gh_id=%d", base_url_buffer, this->gh_id);

    // --- ND URL (Nutrient TDS, pH, Water Temperature, Average Sensor Data URL) ---
    strncpy_P(base_url_buffer, DEFAULT_API_AVG_SENSOR_BASE_URL, sizeof(base_url_buffer) - 1);
    base_url_buffer[sizeof(base_url_buffer) - 1] = '\0';
    snprintf(nd_url, sizeof(nd_url), "%s?gh_id=%d", base_url_buffer, this->gh_id);

    // --- Device Status POST URL ---
    strncpy_P(base_url_buffer, DEFAULT_API_STATUS_POST_BASE_URL, sizeof(base_url_buffer) - 1);
    base_url_buffer[sizeof(base_url_buffer) - 1] = '\0';
    snprintf(device_status_post_url, sizeof(device_status_post_url), "%s?gh_id=%d", base_url_buffer, this->gh_id);

    // --- Device Status GET URL ---
    strncpy_P(base_url_buffer, DEFAULT_API_STATUS_GET_BASE_URL, sizeof(base_url_buffer) - 1);
    base_url_buffer[sizeof(base_url_buffer) - 1] = '\0';
    snprintf(device_status_get_url, sizeof(device_status_get_url), "%s?gh_id=%d", base_url_buffer, this->gh_id);
    
    // --- World Time URL ---
    // This URL is typically common and does not require gh_id.
    strncpy_P(worldtime_url, WORLDTIME_URL, sizeof(worldtime_url) - 1);
    worldtime_url[sizeof(worldtime_url) - 1] = '\0'; // Ensure null termination.
}

/**
 * @brief Loads configuration from NVS or applies firmware defaults.
 *
 * This crucial method initializes the device's configuration by:
 * 1. Attempting to open the NVS under the defined `NVS_NAMESPACE`.
 * 2. For each configurable item (GH_ID, WiFi SSID, WiFi Password, API Token):
 *    a. Try to load the value from NVS.
 *    b. If the value is not found in NVS or is considered invalid (e.g., empty string for SSID,
 *       GH_ID out of range), the corresponding firmware default from `config.h` is applied.
 *       Firmware defaults for WiFi are selected based on the (potentially defaulted) `gh_id`.
 *    c. If a firmware default was applied (either because the NVS key was missing or the NVS value
 *       was invalid and corrected), the new default value is then saved back to NVS to ensure
 *       persistence for subsequent boots.
 * 3. GPRS settings (APN, user, password, SIM PIN) are always loaded directly from firmware defaults
 *    in `config.h` as they are not typically user-configurable via the runtime portal.
 * 4. After all settings are finalized (especially `gh_id`), `constructApiUrls()` is called to
 *    generate the full API endpoint URLs.
 * 5. Closes the NVS if it was successfully opened.
 *
 * This ensures that the device always operates with a valid configuration, prioritizing NVS
 * settings but falling back to and persisting firmware defaults when necessary.
 */
void DeviceConfig::loadConfigFromNvsOrDefaults() {
    bool nvs_initialized = preferences.begin(NVS_NAMESPACE, false); // Open NVS in read/write mode.
    if (!nvs_initialized) {
        // Log an error if NVS cannot be initialized. Configuration will rely on firmware defaults
        // for this session, and saving defaults back to NVS will fail.
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) { // Check if Serial has been initialized
            Serial.println(F("CRITICAL: NVS.begin() failed in loadConfigFromNvsOrDefaults! Config will use defaults and not persist."));
        }
        #endif
    }

    // --- 1. Determine GH_ID (Greenhouse ID) ---
    // Priority: NVS -> Firmware Default. Validate and save back if defaulted or corrected.
    int nvs_gh_id_check = preferences.getInt(NVS_KEY_GH_ID, -1); // Use -1 to check if key exists
    this->gh_id = (nvs_gh_id_check == -1) ? GH_ID_FIRMWARE_DEFAULT : nvs_gh_id_check;

    bool gh_id_is_invalid = (this->gh_id != 1 && this->gh_id != 2);
    if (gh_id_is_invalid) {
        this->gh_id = GH_ID_FIRMWARE_DEFAULT; // Sanitize to a known valid firmware default.
    }

    // Save GH_ID to NVS if:
    // - NVS was initialized successfully, AND
    // - The key was originally missing from NVS (nvs_gh_id_check == -1), OR
    // - The value loaded from NVS was invalid and has been corrected (gh_id_is_invalid was true).
    if (nvs_initialized && (nvs_gh_id_check == -1 || gh_id_is_invalid)) {
        if (!preferences.putInt(NVS_KEY_GH_ID, this->gh_id)) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            if (Serial) Serial.println(F("Warning: Failed to save GH_ID to NVS."));
            #endif
        }
    }

    // --- 2. Load WiFi SSID and Password ---
    // Priority: NVS -> Firmware Default (based on current gh_id). Save back if defaulted.
    String nvs_ssid = preferences.getString(NVS_KEY_SSID, ""); // Default to empty string if not found
    String nvs_pwd = preferences.getString(NVS_KEY_PWD, "");   // Default to empty string if not found
    bool ssid_was_defaulted = false;
    bool pwd_was_defaulted = false;

    // Load SSID
    if (nvs_ssid.length() > 0 && nvs_ssid.length() < sizeof(this->ssid)) {
        strncpy(this->ssid, nvs_ssid.c_str(), sizeof(this->ssid) - 1);
    } else { // NVS SSID is empty or too long, use firmware default
        if (this->gh_id == 1) strncpy_P(this->ssid, DEFAULT_WIFI_SSID_GH1, sizeof(this->ssid) - 1);
        else strncpy_P(this->ssid, DEFAULT_WIFI_SSID_GH2, sizeof(this->ssid) - 1);
        ssid_was_defaulted = true;
    }
    this->ssid[sizeof(this->ssid) - 1] = '\0'; // Ensure null termination

    if (nvs_initialized && ssid_was_defaulted) {
        if (!preferences.putString(NVS_KEY_SSID, this->ssid)) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            if (Serial) Serial.println(F("Warning: Failed to save default SSID to NVS."));
            #endif
        }
    }

    // Load Password
    if (nvs_pwd.length() > 0 && nvs_pwd.length() < sizeof(this->password)) {
        strncpy(this->password, nvs_pwd.c_str(), sizeof(this->password) - 1);
    } else { // NVS Password is empty or too long, use firmware default
        if (this->gh_id == 1) strncpy_P(this->password, DEFAULT_WIFI_PWD_GH1, sizeof(this->password) - 1);
        else strncpy_P(this->password, DEFAULT_WIFI_PWD_GH2, sizeof(this->password) - 1);
        pwd_was_defaulted = true;
    }
    this->password[sizeof(this->password) - 1] = '\0'; // Ensure null termination

    if (nvs_initialized && pwd_was_defaulted) {
        if(!preferences.putString(NVS_KEY_PWD, this->password)) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            if (Serial) Serial.println(F("Warning: Failed to save default Password to NVS."));
            #endif
        }
    }
    
    // --- 3. Load API Token ---
    // Priority: NVS -> Firmware Default. Save back if defaulted.
    String nvs_token = preferences.getString(NVS_KEY_TOKEN, ""); // Default to empty string if not found
    bool token_was_defaulted = false;

    if (nvs_token.length() > 0 && nvs_token.length() < sizeof(this->api_token)) {
        strncpy(this->api_token, nvs_token.c_str(), sizeof(this->api_token) - 1);
    } else { // NVS Token is empty or too long, use firmware default
        strncpy_P(this->api_token, AUTH, sizeof(this->api_token) - 1); // AUTH is from config.h
        token_was_defaulted = true;
    }
    this->api_token[sizeof(this->api_token) - 1] = '\0'; // Ensure null termination

    if (nvs_initialized && token_was_defaulted) {
        if(!preferences.putString(NVS_KEY_TOKEN, this->api_token)) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            if (Serial) Serial.println(F("Warning: Failed to save default API Token to NVS."));
            #endif
        }
    }

    // --- 4. Load GPRS Settings ---
    // GPRS settings are always loaded from firmware defaults (`config.h`).
    // They are not configurable via NVS through the standard mechanisms.
    strncpy_P(this->gprs_apn, GPRS_APN, sizeof(this->gprs_apn) - 1);
    this->gprs_apn[sizeof(this->gprs_apn) - 1] = '\0';

    strncpy_P(this->gprs_user, GPRS_USER, sizeof(this->gprs_user) - 1);
    this->gprs_user[sizeof(this->gprs_user) - 1] = '\0';

    strncpy_P(this->gprs_password, GPRS_PASSWORD, sizeof(this->gprs_password) - 1);
    this->gprs_password[sizeof(this->gprs_password) - 1] = '\0';

    strncpy_P(this->sim_pin, SIM_PIN, sizeof(this->sim_pin) - 1);
    this->sim_pin[sizeof(this->sim_pin) - 1] = '\0';
    
    // --- 5. Construct API URLs ---
    // After all configuration values (especially gh_id) are finalized,
    // construct the full API endpoint URLs using the current `gh_id`.
    constructApiUrls(); // This method uses the finalized this->gh_id

    if (nvs_initialized) {
        preferences.end(); // Close NVS namespace
    }
}

/**
 * @brief Constructor for DeviceConfig.
 *
 * Initializes a new DeviceConfig object by immediately calling `loadConfigFromNvsOrDefaults()`
 * to populate all configuration settings from NVS or firmware defaults.
 */
DeviceConfig::DeviceConfig() {
    loadConfigFromNvsOrDefaults();
}

/**
 * @brief Saves the provided configuration settings to NVS and updates the in-memory config.
 *
 * @param new_gh_id The new Greenhouse ID (must be 1 or 2).
 * @param new_ssid The new WiFi SSID.
 * @param new_password The new WiFi Password.
 * @param new_api_token The new API Authentication Token.
 * @return true if all settings were successfully saved to NVS and in-memory config updated.
 * @return false if `new_gh_id` is invalid, NVS cannot be opened, or any NVS write operation fails.
 *
 * If `new_gh_id` is different from the current `gh_id`, API URLs are reconstructed.
 */
bool DeviceConfig::saveConfig(int new_gh_id, const char* new_ssid, const char* new_password, const char* new_api_token) {
    if (new_gh_id != 1 && new_gh_id != 2) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: Attempted to save invalid GH_ID. Must be 1 or 2."));
        #endif
        return false; // Invalid GH_ID, do not proceed.
    }

    if (!preferences.begin(NVS_NAMESPACE, false)) { // Open NVS in read/write mode.
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: NVS.begin() failed in saveConfig! Cannot save settings."));
        #endif
        return false; // Cannot save if NVS cannot be opened.
    }

    bool all_saves_successful = true;
    bool gh_id_changed = (this->gh_id != new_gh_id);

    // --- Update GH_ID ---
    this->gh_id = new_gh_id;
    if (!preferences.putInt(NVS_KEY_GH_ID, this->gh_id)) {
        all_saves_successful = false;
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.printf("ERROR: Failed to save GH_ID (%d) to NVS.\n", this->gh_id);
        #endif
    }

    // --- Update WiFi SSID ---
    strncpy(this->ssid, new_ssid, sizeof(this->ssid) - 1);
    this->ssid[sizeof(this->ssid) - 1] = '\0'; // Ensure null termination.
    if (!preferences.putString(NVS_KEY_SSID, this->ssid)) {
        all_saves_successful = false;
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: Failed to save SSID to NVS."));
        #endif
    }

    // --- Update WiFi Password ---
    strncpy(this->password, new_password, sizeof(this->password) - 1);
    this->password[sizeof(this->password) - 1] = '\0'; // Ensure null termination.
    if (!preferences.putString(NVS_KEY_PWD, this->password)) {
        all_saves_successful = false;
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: Failed to save Password to NVS."));
        #endif
    }
    
    // --- Update API Token ---
    strncpy(this->api_token, new_api_token, sizeof(this->api_token) - 1);
    this->api_token[sizeof(this->api_token) - 1] = '\0'; // Ensure null termination.
    if (!preferences.putString(NVS_KEY_TOKEN, this->api_token)) {
        all_saves_successful = false;
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: Failed to save API Token to NVS."));
        #endif
    }

    // If GH_ID changed, API URLs need to be reconstructed.
    if (gh_id_changed) {
        constructApiUrls();
    }
    
    preferences.end(); // Close NVS.
    return all_saves_successful;
}

/**
 * @brief Resets all configurable settings in NVS back to their firmware defaults.
 *
 * This function performs a factory reset by:
 * 1. Attempting to open the NVS namespace.
 * 2. Removing the NVS keys for `gh_id`, `ssid`, `password`, `api_token`, and any
 *    deprecated URL keys (`NVS_KEY_OLD_TH_URL`, `NVS_KEY_OLD_ND_URL`).
 * 3. Closing NVS (if opened).
 * 4. Calling `loadConfigFromNvsOrDefaults()`. This method will find the NVS keys missing
 *    (because they were just removed), apply the firmware defaults defined in `config.h`,
 *    and then attempt to save these defaults back into NVS for persistence.
 *
 * If NVS cannot be opened, an error is logged, and the NVS clearing part might be skipped,
 * but `loadConfigFromNvsOrDefaults()` will still attempt to load (and subsequently try to save)
 * defaults, effectively resetting the in-memory configuration.
 */
void DeviceConfig::factoryResetConfig() {
    bool nvs_opened_successfully = preferences.begin(NVS_NAMESPACE, false);
    if (!nvs_opened_successfully) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
        if (Serial) Serial.println(F("ERROR: NVS.begin() failed in factoryResetConfig! NVS clear step may be skipped, but will attempt to load defaults."));
        #endif
        // Even if NVS can't be opened to clear, loadConfigFromNvsOrDefaults() will still set in-memory defaults.
    }
    
    // Remove all user-configurable keys from NVS.
    // If NVS wasn't opened successfully, these `remove` operations will likely fail silently or do nothing,
    // which is acceptable as loadConfigFromNvsOrDefaults will handle missing keys by applying defaults.
    preferences.remove(NVS_KEY_GH_ID);
    preferences.remove(NVS_KEY_SSID);
    preferences.remove(NVS_KEY_PWD);
    preferences.remove(NVS_KEY_TOKEN);

    // Also remove any deprecated keys from older firmware versions to ensure a clean state.
    preferences.remove(NVS_KEY_OLD_TH_URL); // Deprecated: Old user-configurable THD data URL.
    preferences.remove(NVS_KEY_OLD_ND_URL); // Deprecated: Old user-configurable Average Sensor data URL.
    
    if (nvs_opened_successfully) {
        preferences.end(); // Close NVS if it was successfully opened.
    }

    // Reload configuration. This will now apply firmware defaults because the NVS keys
    // were (attempted to be) removed. `loadConfigFromNvsOrDefaults` will also attempt
    // to save these newly applied defaults back to NVS.
    loadConfigFromNvsOrDefaults();
}