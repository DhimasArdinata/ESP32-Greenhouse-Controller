///////////////////////////////////////////////////////////////////////////////////
// File: ESP32_TCall_RelayController_WebConfig_Corrected.ino
//
// Description: Controls 3 low-state relays based on sensor thresholds
//              (Exhaust & Dehumidifier on Humidity, Blower on Temperature)
//              fetched from an API. Relay 4 is unused.
//              Uses WiFi primarily, falls back to GPRS (SIM800L).
//              Includes RTC, SD logging, Network Time Sync (NTP/NITZ/HTTP),
//              API error handling, stale data indication, failsafe mode,
//              runtime SD card failure detection, GPRS signal quality check,
//              Watchdog timer, Web Configuration Portal for WiFi/API settings,
//              and allows manual override of relays via a web API.
//
// Board: TTGO T-Call V1.3/V1.4 (or similar ESP32 + SIM800L board)
//
// Author: Dhimas Ardinata
// Date:   May 2025 (Last Revision)
///////////////////////////////////////////////////////////////////////////////////

// Configuration file should be included first to ensure all project-specific
// defines (like TINY_GSM_MODEM_SIM800) are available before any library includes.
#include "config.h" // Contains PINs, Default Credentials, URLs, Timings etc.

// --- Modem Definition (Moved to config.h) ---
// #define TINY_GSM_MODEM_SIM800
// #define TINY_GSM_DEBUG Serial
// #define DUMP_AT_COMMANDS

// --- Standard Libraries ---
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGsmCommon.h>
#include <TinyGsmClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <functional> // For std::function

// --- Additional Libraries for Advanced Features ---
#include <Preferences.h>    // For saving settings to NVS
// WebServer and DNSServer are now managed by ConfigPortalManager
#include <esp_task_wdt.h> // Include watchdog bawaan ESP32
#include <ESPmDNS.h>        // For mDNS discovery (optional, but often included)
#include <memory>           // For std::unique_ptr

// --- Custom Network Management ---
#include "NetworkInterface.h"
#include "WiFiManager.h"
#include "GPRSManager.h"
#include "NetworkFacade.h"
#include "DeviceConfig.h" // For global config struct
#include "DeviceState.h"  // For global state struct
#include "LCDDisplay.h"   // For LCD Display class
#include "SensorDataManager.h" // For SensorDataManager class
#include "RTCManager.h"   // For RTCManager class
#include "RelayController.h" // For RelayController class
#include "SDCardLogger.h"  // For SDCardLogger class
#include "ConfigPortalManager.h" // For Configuration Portal

// --- Configuration File --- (Moved to the top)
// #include "config.h"

// --- Forward Declarations ---
// Classes will be defined before instances, so these are not strictly needed for classes
// but good practice if order might change.
class LCDDisplay;
// class MyNetworkManager; // Replaced by NetworkFacade
class NetworkFacade;
class SensorDataManager;
class SDCardLogger;
class RTCManager;
class RelayController;

// Functions that might be called before their full definition
void printDebugStatus(const char* msg); // IMPORTANT: Used by many classes/functions
// Config Portal functions (startConfigPortal, handleCaptivePortal, processor, handleFactoryReset) moved to ConfigPortalManager

// Loop helper functions
void handleNetworkConnection(unsigned long now);
void handleApiDataFetching(unsigned long now);
void checkDataStalenessAndFailsafe(unsigned long now);
void handleWebOverride(unsigned long now);
void runMainOperationalBlock(unsigned long now);
void checkSdCard(unsigned long now);
void checkRtcSync(unsigned long now);

// --- Global Configuration and State Instances ---
DeviceConfig deviceConfig; // Holds all persistent configuration
DeviceState deviceState;   // Holds all dynamic operational states

// Web Server and DNS Server objects for Config Portal
// WebServer and DNSServer objects are now part of ConfigPortalManager
// Preferences object is now encapsulated within DeviceConfig

// --- Global Modem Instance (required by GPRSManager) ---
TinyGsm modem(Serial1); // RX: GSM_RX, TX: GSM_TX (defined in config.h, used by Serial1.begin)

// Note: Individual global state variables (lastLoop, active_ssid, etc.) are now part of
// deviceConfig and deviceState structs.

// ==================================================================================
//   LCD Display Class Definition (Moved to LCDDisplay.h and LCDDisplay.cpp)
// ==================================================================================

// ==================================================================================
//   Debug Function (Simplified) Definition (Moved to config.h)
// ==================================================================================

// ==================================================================================
//   Sensor Data Manager Class Definition (Moved to SensorDataManager.h and SensorDataManager.cpp)
// ==================================================================================

// ==================================================================================
//   RTC Manager Class Definition (Moved to RTCManager.h and RTCManager.cpp)
// ==================================================================================

// ==================================================================================
//   Relay Controller Class Definition (Moved to RelayController.h and RelayController.cpp)
// ==================================================================================

// ==================================================================================
//   SD Card Logger Class Definition (Moved to SDCardLogger.h and SDCardLogger.cpp)
// ==================================================================================

// --- Global Class Instances ---
// These MUST be declared AFTER the full class definitions above.
SensorDataManager sensorData;
LCDDisplay lcd; // This is the single global LCD object.
// MyNetworkManager* net = nullptr; // Replaced by NetworkFacade
NetworkFacade* networkFacade = nullptr; // Global instance for the network facade
RTCManager* rtc_mgr = nullptr;
RelayController relay(lcd); // Pass the global lcd object by reference.
SDCardLogger sd_logger(&lcd); // SDCardLogger now manages its own 'ok' status internally
ConfigPortalManager* configPortalMgr = nullptr; // Global instance for Config Portal Manager
char globalDateTimeBuffer[20]; // Buffer for RTCManager::getDateTimeString


// ==================================================================================
//   Function Definitions (Global Functions)
// ==================================================================================
void printDebugStatus(const char* msg) {
    DEBUG_PRINTLN(3, msg);
    lcd.message(0, 0, msg, true); // 'lcd' is now the globally defined object
}

// loadConfiguration() and saveConfiguration() are removed.
// DeviceConfig handles its own NVS loading on construction and saving via its methods.

// CONFIG_PAGE, processor, handleCaptivePortal, startConfigPortal, and handleFactoryReset
// have been moved to ConfigPortalManager.cpp and ConfigPortalManager.h

// ==================================================================================
//   Setup Function
// ==================================================================================
void setup() {
    Serial.begin(115200); while (!Serial && millis() < 2000);
    Serial.println(F("\n\n--- ESP32 T-Call Relay Controller Starting ---"));
    esp_task_wdt_reset();
 
    // esp_task_wdt_config_t wdt_config; // Declaration moved up or ensure include is effective
    // wdt_config.timeout_ms=WDT_TIMEOUT*1000;
    // wdt_config.idle_core_mask=(1<<0)|(1<<1);
    // wdt_config.trigger_panic=true;
    // esp_err_t init_err=esp_task_wdt_init(&wdt_config);
    // The esp_task_wdt_config_t error might be due to include order or other compile issues.
    // For now, commenting out to see if other fixes allow compilation.
    // If this is critical, the include for esp_task_wdt.h (line 50) needs to be verified.
    // A minimal WDT init without config struct for now, if possible, or ensure type is known.
    // Reverting to a simpler init if the config struct is the issue.
    esp_err_t init_err = esp_task_wdt_init(WDT_TIMEOUT * 1000, true); // Simplified init
    if(init_err==ESP_OK){init_err=esp_task_wdt_add(NULL); if(init_err!=ESP_OK)Serial.printf("WDT Add fail:%s\n",esp_err_to_name(init_err)); else Serial.println("WDT Init OK");}
    else Serial.printf("WDT Init fail:%s\n",esp_err_to_name(init_err));
    esp_task_wdt_reset();

    Wire.begin(SDA_PIN, SCL_PIN); esp_task_wdt_reset();
    lcd.begin(); printDebugStatus("Setup Starting..."); esp_task_wdt_reset(); // lcd is global
    // loadConfiguration(); // Removed: DeviceConfig loads itself in its constructor.
    sd_logger.begin(); esp_task_wdt_reset(); // Use sd_logger
    relay.begin(); esp_task_wdt_reset();

    // Instantiate WiFiManager
    auto wifiManager = std::unique_ptr<WiFiManager>(new WiFiManager(deviceConfig.ssid, deviceConfig.password, deviceConfig.api_token, &lcd));
    esp_task_wdt_reset();

    // Instantiate GPRSManager
    // 'modem' is the global TinyGsm instance
    auto gprsManager = std::unique_ptr<GPRSManager>(new GPRSManager(modem, deviceConfig.gprs_apn, deviceConfig.gprs_user, deviceConfig.gprs_password, deviceConfig.sim_pin, deviceConfig.api_token, &deviceState, &lcd));
    esp_task_wdt_reset();

    // Instantiate NetworkFacade, taking ownership of wifiManager and gprsManager
    // Defaulting to WiFi preferred. This can be made configurable later if needed.
    networkFacade = new NetworkFacade(NetworkFacade::NetworkPreference::WIFI_PREFERRED, std::move(wifiManager), std::move(gprsManager), &deviceState);
    if (!networkFacade) {
        printDebugStatus("FATAL: NetworkFacade init failed!");
        while (1) { esp_task_wdt_reset(); delay(1000); } // Halt
    }
    esp_task_wdt_reset();
    
    // Instantiate ConfigPortalManager
    configPortalMgr = new ConfigPortalManager(deviceConfig, lcd, networkFacade);
    if (!configPortalMgr) {
        printDebugStatus("FATAL: ConfigPortalManager init failed!");
        while(1) { esp_task_wdt_reset(); delay(1000); } // Halt
    }
    esp_task_wdt_reset();

    printDebugStatus("Starting Network (Facade)...");
    if (!networkFacade->connect()) { // connect() will try based on preference
        printDebugStatus("Initial connection failed. Starting Config Portal.");
        // Fallback to config portal if no connection
        // The startPortal() method is blocking and will ESP.restart() on completion/timeout.
        configPortalMgr->startPortal();
        // Code here will not be reached if portal starts, as it ends with ESP.restart()
    } else {
        printDebugStatus(networkFacade->getStatusString().c_str());
    }
    esp_task_wdt_reset();

    // RTCManager now needs a NetworkInterface compatible reference.
    // The NetworkFacade itself is a NetworkInterface.
    rtc_mgr = new RTCManager(lcd, *networkFacade); // Pass NetworkFacade instance
    if(!rtc_mgr){while(1){esp_task_wdt_reset();delay(1000);}} esp_task_wdt_reset();
    rtc_mgr->begin(); if(rtc_mgr->isRtcOk() && networkFacade && networkFacade->isConnected())rtc_mgr->checkAndSyncOnDrift(); esp_task_wdt_reset();

    if(sd_logger.isSdCardOk()){if(sensorData.loadFromLog())printDebugStatus("Log Data Loaded");else printDebugStatus("Log Load Failed");}else printDebugStatus("No SD for Init");
    esp_task_wdt_reset();
 
    if(networkFacade && networkFacade->isConnected()){
        // Fetch initial device statuses
        networkFacade->startAsyncHttpRequest(deviceConfig.device_status_get_url, "GET", "DEV_ST_G_SETUP", nullptr,
            [&](JsonDocument& doc) -> bool { // Capture deviceState by reference
            if (!doc["data"].isNull() && doc["data"].is<JsonObject>()) {
                JsonObject data = doc["data"];
                if (!data["exhaust_status"].isNull() && !data["dehumidifier_status"].isNull() && !data["blower_status"].isNull()) {
                    deviceState.web_exhaust_target_state = atoi(data["exhaust_status"].as<const char*>()) == 1;
                    deviceState.web_dehumidifier_target_state = atoi(data["dehumidifier_status"].as<const char*>()) == 1;
                    deviceState.web_blower_target_state = atoi(data["blower_status"].as<const char*>()) == 1;
                    // Initialize last states to current states to prevent immediate override on first loop
                    deviceState.last_web_exhaust_target_state = deviceState.web_exhaust_target_state;
                    deviceState.last_web_dehumidifier_target_state = deviceState.web_dehumidifier_target_state;
                    deviceState.last_web_blower_target_state = deviceState.web_blower_target_state;
                    DEBUG_PRINTLN_F(3, F("Async DEV_ST_G_SETUP CB: Initial web statuses updated."));
                    return true;
                } else { DEBUG_PRINTLN_F(1, F("Async DEV_ST_G_SETUP CB: JSON missing status fields.")); }
            } else { DEBUG_PRINTLN_F(1, F("Async DEV_ST_G_SETUP CB: Malformed JSON.")); }
            return false;
        }, true);
    }
    esp_task_wdt_reset();
 
    if(networkFacade && networkFacade->isConnected()){
        printDebugStatus("Fetching initial API data (async)...");
        // Thresholds
        networkFacade->startAsyncHttpRequest(deviceConfig.th_url, "GET", "TH_ASYNC_SETUP", nullptr,
            [&](JsonDocument& d) -> bool { // Capture deviceState by reference
            if (d["data"].isNull() || !d["data"].is<JsonArray>()) { DEBUG_PRINTLN_F(1, F("Async TH_SETUP CB: Malformed JSON.")); return false; }
            JsonArray da = d["data"].as<JsonArray>();
            int fc = 0; float tMn = 0, tMx = 0, hMn = 0, hMx = 0, lMn = 0, lMx = 0;
            for (JsonObject i : da) {
                if (i["name"].isNull() || i["threshold_min"].isNull() || i["threshold_max"].isNull()) continue;
                const char* n = i["name"];
                float mn = atof(i["threshold_min"].as<const char*>()); float mx = atof(i["threshold_max"].as<const char*>());
                if (strcmp(n, "Temperature") == 0) { tMn = mn; tMx = mx; fc++; }
                else if (strcmp(n, "Humidity") == 0) { hMn = mn; hMx = mx; fc++; }
                else if (strcmp(n, "Light Intensity") == 0) { lMn = mn; lMx = mx; fc++; }
            }
            if (fc < 3) { DEBUG_PRINTF(1, "Async TH_SETUP CB: Missing thresholds. Found: %d\n", fc); return false; }
            sensorData.updateThresholds(tMn, tMx, hMn, hMx, lMn, lMx);
            DEBUG_PRINTLN_F(3, F("Async TH_SETUP CB: Thresholds updated."));
            deviceState.lastSuccessfulApiUpdateTime = millis();
            if (deviceState.isInFailSafeMode) deviceState.isInFailSafeMode = false;
            return true;
        }, true);
        esp_task_wdt_reset();
        // Node Data
        networkFacade->startAsyncHttpRequest(deviceConfig.nd_url, "GET", "ND_ASYNC_SETUP", nullptr,
            [&](JsonDocument& d) -> bool { // Capture deviceState by reference
            if (d["data"].isNull() || !d["data"].is<JsonObject>()) { DEBUG_PRINTLN_F(1, F("Async ND_SETUP CB: Malformed JSON.")); return false; }
            JsonObject o = d["data"];
            if (o["temperature"].isNull() || o["humidity"].isNull() || o["light_intensity"].isNull()) { DEBUG_PRINTLN_F(1, F("Async ND_SETUP CB: JSON missing fields.")); return false; }
            sensorData.updateData(o["temperature"].as<float>(), o["humidity"].as<float>(), o["light_intensity"].as<float>());
            DEBUG_PRINTLN_F(3, F("Async ND_SETUP CB: Node data updated."));
            deviceState.lastSuccessfulApiUpdateTime = millis();
            if (deviceState.isInFailSafeMode) deviceState.isInFailSafeMode = false;
            return true;
        }, true);
    }
    else printDebugStatus("No Net for initial API fetch");
    esp_task_wdt_reset();

    // Initialize DeviceState timers
    unsigned long m = millis();
    deviceState.lastLoopTime = m;
    deviceState.lastApiAttemptTime = m - API_MS + 5000; // Allow first attempt soon
    deviceState.lastTimeSyncTime = m;
    deviceState.lastSdRetryTime = m;
    deviceState.lastConnectionRetryTime = m;
    deviceState.lastWiFiRetryWhenGprsTime = m;
    deviceState.lastDeviceStatusCheckTime = m;
    // deviceState.currentConnectionRetryDelayMs is initialized in its constructor

    printDebugStatus("Setup Complete"); esp_task_wdt_reset();
}

// ==================================================================================
//   Main Loop Function
// ==================================================================================
void loop() {
    esp_task_wdt_reset();
    if (!networkFacade || !rtc_mgr) {
        printDebugStatus("FATAL: networkFacade or rtc_mgr null in loop!");
        delay(1000); ESP.restart();
    }

    unsigned long now = millis();

    // Update all ongoing asynchronous network operations
    if (networkFacade) networkFacade->updateHttpOperations();

    // Update GPRS Finite State Machine
    if (networkFacade && networkFacade->getGPRSManager()) {
        networkFacade->getGPRSManager()->updateFSM();
    }

    // Call helper functions
    handleNetworkConnection(now);
    handleApiDataFetching(now);
    checkDataStalenessAndFailsafe(now);
    handleWebOverride(now);
    runMainOperationalBlock(now);
    checkSdCard(now);
    checkRtcSync(now);
    
    yield();
}
// ==================================================================================
//   Loop Helper Function Definitions
// ==================================================================================

void handleNetworkConnection(unsigned long now) {
    if (networkFacade && !networkFacade->isConnected() && (now - deviceState.lastConnectionRetryTime >= deviceState.currentConnectionRetryDelayMs)) {
        deviceState.lastConnectionRetryTime = now;
        printDebugStatus("Attempting network reconnect...");
        if (networkFacade->connect()) { // Facade's connect handles preference
            deviceState.currentConnectionRetryDelayMs = INITIAL_RETRY_DELAY_MS; // From config.h
            DEBUG_PRINTF(3, "Net Reconnect OK: %s", networkFacade->getStatusString().c_str());
            lcd.message(0,0, "Net Reconnect OK", true); // Simplified for LCD line 0
            // lcd.message(0,1, networkFacade->getStatusString().c_str(), true); // Optionally display full status on next line if space
            if (rtc_mgr && rtc_mgr->isRtcOk()) rtc_mgr->checkAndSyncOnDrift();
        } else {
            deviceState.currentConnectionRetryDelayMs *= 2;
            if (deviceState.currentConnectionRetryDelayMs > MAX_RETRY_DELAY_MS) deviceState.currentConnectionRetryDelayMs = MAX_RETRY_DELAY_MS; // From config.h
            printDebugStatus("Net Reconnect Wait...");
        }
    }

    // If GPRS is active, periodically try to switch back to WiFi if WiFi is preferred and available
    if (networkFacade && networkFacade->getPreference() == NetworkFacade::NetworkPreference::WIFI_PREFERRED &&
        networkFacade->getWiFiManager() != nullptr && 
        networkFacade->getCurrentInterface() == networkFacade->getGPRSManager() &&
        networkFacade->isConnected() && 
        (now - deviceState.lastWiFiRetryWhenGprsTime >= deviceState.currentWiFiSwitchBackoffDelayMs)) { // Use backoff delay
        deviceState.lastWiFiRetryWhenGprsTime = now;
        DEBUG_PRINTF(3, "Attempting to switch back to WiFi (backoff: %lu ms)...", deviceState.currentWiFiSwitchBackoffDelayMs);
        // lcd.message(0,1,"Try WiFi Switch...",true); // Optional brief LCD indicator
        if (networkFacade->switchToWiFi()) {
            DEBUG_PRINTF(3, "Switched to WiFi: %s", networkFacade->getStatusString().c_str());
            lcd.message(0,0, "Switched to WiFi", true);
            if (rtc_mgr && rtc_mgr->isRtcOk()) rtc_mgr->checkAndSyncOnDrift();
            deviceState.currentWiFiSwitchBackoffDelayMs = WIFI_RETRY_WHEN_GPRS_MS; // Reset backoff on success
        } else {
            DEBUG_PRINTLN_F(2, F("Failed to switch back to WiFi, staying on GPRS. Increasing backoff."));
            // lcd.message(0,1,"WiFi Switch Fail",true); // Optional brief LCD indicator
            deviceState.currentWiFiSwitchBackoffDelayMs *= 2;
            if (deviceState.currentWiFiSwitchBackoffDelayMs > MAX_WIFI_RETRY_WHEN_GPRS_MS) {
                deviceState.currentWiFiSwitchBackoffDelayMs = MAX_WIFI_RETRY_WHEN_GPRS_MS;
            }
            DEBUG_PRINTF(3, "Next WiFi switch attempt in %lu ms.", deviceState.currentWiFiSwitchBackoffDelayMs);
        }
    }
}

void handleApiDataFetching(unsigned long now) {
    if (networkFacade && networkFacade->isConnected() && (now - deviceState.lastApiAttemptTime >= API_MS)) { // API_MS from config.h
        deviceState.lastApiAttemptTime = now;
        // Thresholds
        bool th_initiated = networkFacade->startAsyncHttpRequest(deviceConfig.th_url, "GET", "TH_ASYNC_LP", nullptr,
            [&](JsonDocument& d) -> bool { 
            if (d["data"].isNull() || !d["data"].is<JsonArray>()) {
                DEBUG_PRINTLN_F(1, F("Async TH LP CB: Malformed JSON - no data array."));
                return false;
            }
            JsonArray da = d["data"].as<JsonArray>();
            int fc = 0; float tMn = 0, tMx = 0, hMn = 0, hMx = 0, lMn = 0, lMx = 0;
            for (JsonObject i : da) {
                if (i["name"].isNull() || i["threshold_min"].isNull() || i["threshold_max"].isNull()) continue;
                const char* n = i["name"];
                float mn = atof(i["threshold_min"].as<const char*>());
                float mx = atof(i["threshold_max"].as<const char*>());
                if (strcmp(n, "Temperature") == 0) { tMn = mn; tMx = mx; fc++; }
                else if (strcmp(n, "Humidity") == 0) { hMn = mn; hMx = mx; fc++; }
                else if (strcmp(n, "Light Intensity") == 0) { lMn = mn; lMx = mx; fc++; }
            }
            if (fc < 3) {
                 DEBUG_PRINTF(1, "Async TH LP CB: Failed to find all 3 thresholds. Found: %d\n", fc);
                return false;
            }
            sensorData.updateThresholds(tMn, tMx, hMn, hMx, lMn, lMx);
            DEBUG_PRINTLN_F(3, F("Async TH LP CB: Thresholds updated."));
            deviceState.lastSuccessfulApiUpdateTime = millis();
            if (deviceState.isInFailSafeMode) { deviceState.isInFailSafeMode = false; printDebugStatus("Exited Failsafe (API TH OK).");}
            return true;
        }, true);

        // Node Data
        bool nd_initiated = networkFacade->startAsyncHttpRequest(deviceConfig.nd_url, "GET", "ND_ASYNC_LP", nullptr,
            [&](JsonDocument& d) -> bool { 
            if (d["data"].isNull() || !d["data"].is<JsonObject>()) {
                DEBUG_PRINTLN_F(1, F("Async ND LP CB: Malformed JSON - no data object."));
                return false;
            }
            JsonObject o = d["data"];
            if (o["temperature"].isNull() || o["humidity"].isNull() || o["light_intensity"].isNull()) {
                DEBUG_PRINTLN_F(1, F("Async ND LP CB: JSON missing sensor fields."));
                return false;
            }
            sensorData.updateData(o["temperature"].as<float>(), o["humidity"].as<float>(), o["light_intensity"].as<float>());
            DEBUG_PRINTLN_F(3, F("Async ND LP CB: Node data updated."));
            deviceState.lastSuccessfulApiUpdateTime = millis();
            if (deviceState.isInFailSafeMode) { deviceState.isInFailSafeMode = false; printDebugStatus("Exited Failsafe (API ND OK).");}
            return true;
        }, true);

        if (th_initiated) DEBUG_PRINTLN(3, "Async Threshold fetch (loop) initiated.");
        if (nd_initiated) DEBUG_PRINTLN(3, "Async Node Data fetch (loop) initiated.");
    }
}

void checkDataStalenessAndFailsafe(unsigned long now) {
    bool iDS = false; // isDataStale
    if (deviceState.lastSuccessfulApiUpdateTime == 0 && millis() > STALE_DATA_THRESHOLD_MS) iDS = true; // From config.h
    else if (deviceState.lastSuccessfulApiUpdateTime > 0 && (now - deviceState.lastSuccessfulApiUpdateTime > STALE_DATA_THRESHOLD_MS)) iDS = true;

    if (iDS && !deviceState.isInFailSafeMode) DEBUG_PRINTLN(2, "Warn: API data stale.");
    
    if (deviceState.lastSuccessfulApiUpdateTime > 0 && (now - deviceState.lastSuccessfulApiUpdateTime > FAILSAFE_TIMEOUT_MS) && !deviceState.isInFailSafeMode) { // From config.h
        deviceState.isInFailSafeMode = true;
        relay.forceSafeState();
        printDebugStatus("FAILSAFE Active!");
    }
}

void handleWebOverride(unsigned long now) {
    // Fetch web override status
    if (networkFacade && networkFacade->isConnected() && (now - deviceState.lastDeviceStatusCheckTime >= DEVICE_STATUS_CHECK_INTERVAL_MS)) { // From config.h
        deviceState.lastDeviceStatusCheckTime = now;
        networkFacade->startAsyncHttpRequest(deviceConfig.device_status_get_url, "GET", "DEV_ST_G_LP_ASYNC", nullptr,
            [&](JsonDocument& doc) -> bool { 
            if (!doc["data"].isNull() && doc["data"].is<JsonObject>()) {
                JsonObject data = doc["data"];
                if (!data["exhaust_status"].isNull() && !data["dehumidifier_status"].isNull() && !data["blower_status"].isNull()) {
                    deviceState.web_exhaust_target_state = atoi(data["exhaust_status"].as<const char*>()) == 1;
                    deviceState.web_dehumidifier_target_state = atoi(data["dehumidifier_status"].as<const char*>()) == 1;
                    deviceState.web_blower_target_state = atoi(data["blower_status"].as<const char*>()) == 1;
                    DEBUG_PRINTLN_F(3, F("Async DEV_ST_G LP CB: Web statuses updated."));
                    return true;
                } else { DEBUG_PRINTLN_F(1, F("Async DEV_ST_G LP CB: JSON missing status fields.")); }
            } else { DEBUG_PRINTLN_F(1, F("Async DEV_ST_G LP CB: Malformed JSON.")); }
            return false;
        }, true);
    }

    // Apply web override if state changed
    if (deviceState.web_exhaust_target_state != deviceState.last_web_exhaust_target_state) {
        relay.setManualOverride(0, deviceState.web_exhaust_target_state, MANUAL_OVERRIDE_DURATION_MS); // From config.h
        deviceState.last_web_exhaust_target_state = deviceState.web_exhaust_target_state;
        DEBUG_PRINTLN(2, "Loop: Exhaust override updated by web status.");
    }
    if (deviceState.web_dehumidifier_target_state != deviceState.last_web_dehumidifier_target_state) {
        relay.setManualOverride(1, deviceState.web_dehumidifier_target_state, MANUAL_OVERRIDE_DURATION_MS);
        deviceState.last_web_dehumidifier_target_state = deviceState.web_dehumidifier_target_state;
        DEBUG_PRINTLN(2, "Loop: Dehumidifier override updated by web status.");
    }
    if (deviceState.web_blower_target_state != deviceState.last_web_blower_target_state) {
        relay.setManualOverride(2, deviceState.web_blower_target_state, MANUAL_OVERRIDE_DURATION_MS);
        deviceState.last_web_blower_target_state = deviceState.web_blower_target_state;
        DEBUG_PRINTLN(2, "Loop: Blower override updated by web status.");
    }
}

void runMainOperationalBlock(unsigned long now) {
    // Determine if data is stale for display purposes before the main operational block
    bool isDataStaleForDisplay = false;
    if (deviceState.lastSuccessfulApiUpdateTime == 0 && millis() > STALE_DATA_THRESHOLD_MS) isDataStaleForDisplay = true;
    else if (deviceState.lastSuccessfulApiUpdateTime > 0 && (now - deviceState.lastSuccessfulApiUpdateTime > STALE_DATA_THRESHOLD_MS)) isDataStaleForDisplay = true;

    if (now - deviceState.lastLoopTime >= LOOP_MS) { // LOOP_MS from config.h
        deviceState.lastLoopTime = now;
        if (rtc_mgr && rtc_mgr->isRtcOk()) {
            String dt_str = rtc_mgr->getFormattedDateTime();
            strncpy(globalDateTimeBuffer, dt_str.c_str(), sizeof(globalDateTimeBuffer) - 1);
            globalDateTimeBuffer[sizeof(globalDateTimeBuffer) - 1] = '\0'; // Ensure null termination
        }

        if (!deviceState.isInFailSafeMode) {
            bool r1c = relay.updateSingleRelayState(0, sensorData.humidity, sensorData.getHumMin(), sensorData.getHumMax(), sensorData.temperature, sensorData.getTempMin(), sensorData.getTempMax());
            bool r2c = relay.updateSingleRelayState(1, sensorData.humidity, sensorData.getHumMin(), sensorData.getHumMax(), sensorData.temperature, sensorData.getTempMin(), sensorData.getTempMax());
            bool r3c = relay.updateSingleRelayState(2, sensorData.humidity, sensorData.getHumMin(), sensorData.getHumMax(), sensorData.temperature, sensorData.getTempMin(), sensorData.getTempMax());
            relay.ensureRelay4Off();

            if (networkFacade && networkFacade->isConnected()) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                StaticJsonDocument<JSON_DOC_SIZE_STATUS_POST> postJsonDocLocal;
#pragma GCC diagnostic pop
                char payloadBuffer[JSON_DOC_SIZE_STATUS_POST];
                if (r1c) {
                    postJsonDocLocal.clear(); postJsonDocLocal["gh_id"] = deviceConfig.gh_id; postJsonDocLocal["exhaust_status"] = relay.getR1() ? 1 : 0;
                    serializeJson(postJsonDocLocal, payloadBuffer, sizeof(payloadBuffer));
                    networkFacade->startAsyncHttpRequest(deviceConfig.device_status_post_url, "POST", "EXH_ST_P_LP", payloadBuffer, nullptr, true);
                }
                if (r2c) {
                    postJsonDocLocal.clear(); postJsonDocLocal["gh_id"] = deviceConfig.gh_id; postJsonDocLocal["dehumidifier_status"] = relay.getR2() ? 1 : 0;
                    serializeJson(postJsonDocLocal, payloadBuffer, sizeof(payloadBuffer));
                    networkFacade->startAsyncHttpRequest(deviceConfig.device_status_post_url, "POST", "DEH_ST_P_LP", payloadBuffer, nullptr, true);
                }
                if (r3c) {
                    postJsonDocLocal.clear(); postJsonDocLocal["gh_id"] = deviceConfig.gh_id; postJsonDocLocal["blower_status"] = relay.getR3() ? 1 : 0;
                    serializeJson(postJsonDocLocal, payloadBuffer, sizeof(payloadBuffer));
                    networkFacade->startAsyncHttpRequest(deviceConfig.device_status_post_url, "POST", "BLW_ST_P_LP", payloadBuffer, nullptr, true);
                }
            }
        } else {
            relay.forceSafeState();
        }

        if (rtc_mgr) {
             lcd.update(globalDateTimeBuffer, sensorData.temperature, sensorData.humidity, sensorData.light, relay.getR1(), relay.getR2(), relay.getR3(), relay.getR4(), sensorData.getTempMin(), sensorData.getTempMax(), sensorData.getHumMin(), sensorData.getHumMax(), sensorData.getLightMin(), sensorData.getLightMax(), (networkFacade ? networkFacade->isConnected() : false), isDataStaleForDisplay, sd_logger.isSdCardOk(), deviceState.isInFailSafeMode);

            if (sd_logger.isSdCardOk() && rtc_mgr->isRtcOk() && (globalDateTimeBuffer[0] != 'Y' && globalDateTimeBuffer[0] != '\0')) // Check for valid time string
                sd_logger.logData(globalDateTimeBuffer, sensorData.temperature, sensorData.humidity, sensorData.light, sensorData.getTempMin(), sensorData.getTempMax(), sensorData.getHumMin(), sensorData.getHumMax(), sensorData.getLightMin(), sensorData.getLightMax(), relay.getR1(), relay.getR2(), relay.getR3(), relay.getR4());
        }
    }
}

void checkSdCard(unsigned long now) {
    if (!sd_logger.isSdCardOk() && (now - deviceState.lastSdRetryTime >= SD_RETRY_INTERVAL_MS)) { // From config.h
        deviceState.lastSdRetryTime = now;
        printDebugStatus("Retrying SD...");
        // sd_logger.reInit() will update its internal _sdCardOk status
        if (sd_logger.reInit()) DEBUG_PRINTLN(2, "SD re-init OK");
        else DEBUG_PRINTLN(1, "SD re-init Fail");
    }
}

void checkRtcSync(unsigned long now) {
    if (rtc_mgr && rtc_mgr->isRtcOk() && networkFacade && networkFacade->isConnected() && (now - deviceState.lastTimeSyncTime >= TIME_SYNC_INTERVAL)) { // From config.h
        deviceState.lastTimeSyncTime = now;
        NetworkInterface* currentNetIF = networkFacade->getCurrentInterface();
        WiFiManager* wifiMgr = networkFacade->getWiFiManager();

        bool isCurrentWiFi = (wifiMgr && currentNetIF == wifiMgr && wifiMgr->isConnected());

        if (isCurrentWiFi) {
            if (!rtc_mgr->syncNTP()) { 
                DEBUG_PRINTLN(2, "NTP Sync failed, attempting HTTP time sync (async).");
                networkFacade->startAsyncHttpRequest(deviceConfig.worldtime_url, "GET", "WT_ASYNC_LP_WIFI", nullptr,
                [&](JsonDocument& d) { 
                    if (!d["unixtime"].isNull() && d["unixtime"].is<uint32_t>()) {
                        uint32_t epoch = d["unixtime"].as<uint32_t>();
                        if(rtc_mgr) rtc_mgr->adjustTime(epoch); 
                        DEBUG_PRINTF(3, "Async WT LP CB (WiFi): Epoch fetched & RTC adjusted: %lu\n", epoch);
                        return true;
                    }
                    DEBUG_PRINTLN_F(1, F("Async WT LP CB (WiFi): Failed to parse unixtime."));
                    return false;
                }, false);
            }
        } else if (currentNetIF && currentNetIF->isConnected()) { 
            DEBUG_PRINTLN(2, "GPRS/Other active, attempting NITZ/HTTP time sync (async).");
            networkFacade->startAsyncHttpRequest(deviceConfig.worldtime_url, "GET", "WT_ASYNC_LP_GPRS", nullptr,
            [&](JsonDocument& d) { 
                if (!d["unixtime"].isNull() && d["unixtime"].is<uint32_t>()) {
                    uint32_t epoch = d["unixtime"].as<uint32_t>();
                    if(rtc_mgr) rtc_mgr->adjustTime(epoch); 
                    DEBUG_PRINTF(3, "Async WT LP CB (GPRS/Other): Epoch fetched & RTC adjusted: %lu\n", epoch);
                    return true;
                }
                DEBUG_PRINTLN_F(1, F("Async WT LP CB (GPRS/Other): Failed to parse unixtime."));
                return false;
            }, false);
        }
    }
}