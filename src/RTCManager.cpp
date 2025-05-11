#include "RTCManager.h"
#include "NetworkFacade.h" // Required for dynamic_cast and getWiFiManager
#include "WiFiManager.h"   // Required for checking WiFiManager type
#include "config.h"        // For DEBUG_PRINTLN, DEBUG_PRINTF (even if currently commented out)
#include <ArduinoJson.h>   // For JsonDocument in HTTP callback
#include <Wire.h>          // For RTC communication

// Constructor
RTCManager::RTCManager(LCDDisplay& d, NetworkFacade& facade) :
    _lcd_ref(d),                // Renamed
    _networkFacade(facade),
    _timeClient(_ntpUDP),       // Renamed members
    _rtcOk(false) { // Initialize internal flag
    // dateTime member removed, no initialization needed here.
}

bool RTCManager::begin() {
    _lcd_ref.message(0,0, "Init RTC...", true);
    // Wire.begin(SDA_PIN, SCL_PIN); // Wire.begin() is typically called once in global setup.
    // Ensure it's called before RTCManager::begin() if not here.
    if (!_rtc.begin()) {
        _lcd_ref.message(0,1, "RTC HW Failed!", true);
        _rtcOk = false;
        return false;
    }
    _rtcOk = true;
    if (_rtc.lostPower()) {
        _lcd_ref.message(0,1, "RTC Power Lost", true);
        initialTimeSync();
    } else {
        DateTime n = _rtc.now();
        if (n.year() < 2023) { // Basic sanity check for time
            _lcd_ref.message(0,1, "RTC Time Invalid", true);
            initialTimeSync();
        } else {
            _lcd_ref.message(0,1, "RTC Power OK", true);
        }
    }
    return true;
}

void RTCManager::initialTimeSync() {
    if (_networkFacade.isConnected()) {
        _lcd_ref.message(0,2, "Attempting Sync...", true);
        WiFiManager* wifiMgr = _networkFacade.getWiFiManager();
        // Check if current active interface is WiFi and it's connected
        if (wifiMgr && _networkFacade.getCurrentInterface() == wifiMgr && wifiMgr->isConnected()) {
            syncNTP();
        } else {
            syncNITZ(); // Try NITZ/HTTP if on GPRS or other connected interface
        }
    } else {
        _lcd_ref.message(0,2, "RTC: No Net for Sync", true);
    }
}

void RTCManager::adjustTime(uint32_t epoch) {
    if (!_rtcOk) return;
    if (epoch > 1672531200UL) { // Check if epoch is somewhat valid (after 2023-01-01)
        _rtc.adjust(DateTime(epoch));
        // DEBUG_PRINTLN(3, "RTC Time Adjusted via Epoch."); // Use global debug or lcd
        _lcd_ref.message(0,3, "RTC Time Adjusted", true);
    } else {
        // DEBUG_PRINTLN(1, "RTC: Invalid epoch received.");
        _lcd_ref.message(0,3, "RTC: Invalid Epoch", true);
    }
}

bool RTCManager::checkAndSyncOnDrift(uint32_t thresholdSeconds) {
    if (!_rtcOk) return false;
    DateTime ntpTime;
    if (_getNetworkDateTimeNTP(ntpTime)) {
        DateTime rtcTime = _rtc.now();
        long drift = abs((long)rtcTime.unixtime() - (long)ntpTime.unixtime());
        if (drift > thresholdSeconds) {
            // DEBUG_PRINTF(2, "RTC Drift Detected! %ld s. Syncing...\n", drift);
            _lcd_ref.message(0,3, "RTC Drift! Sync...", true);
            adjustTime(ntpTime.unixtime());
            return true;
        }
    }
    return false;
}

bool RTCManager::syncNTP() {
    if (!_rtcOk) return false;
    
    // NetworkFacade* facade = dynamic_cast<NetworkFacade*>(&network_interface); // No longer needed, _networkFacade is NetworkFacade&
    WiFiManager* wifiMgr = _networkFacade.getWiFiManager();

    if (!(wifiMgr && _networkFacade.getCurrentInterface() == wifiMgr && wifiMgr->isConnected())) {
        if (_networkFacade.isConnected()) {
            // DEBUG_PRINTLN(2, "Not on WiFi for NTP, trying HTTP time...");
            _lcd_ref.message(0,2, "NTP Fail, HTTP Sync", true);
            triggerHttpTimeSync();
        } else {
            // DEBUG_PRINTLN(1, "NTP Sync: No WiFi connection.");
            _lcd_ref.message(0,2, "NTP: No WiFi", true);
        }
        return false;
    }
    
    // DEBUG_PRINTLN(3, "Syncing NTP...");
    _lcd_ref.message(0,2, "Syncing NTP...", true);
    _timeClient.begin(); 
    _timeClient.setTimeOffset(NTP_TIMEZONE_OFFSET_SECONDS); 
    if (_timeClient.forceUpdate()) {
        unsigned long epochTime = _timeClient.getEpochTime();
        adjustTime(epochTime); 
        // DEBUG_PRINTLN(3, "NTP Sync OK");
        _lcd_ref.message(0,3, "NTP Sync OK", true);
        return true;
    }
    // DEBUG_PRINTLN(1, "NTP Sync Failed, trying HTTP time...");
    _lcd_ref.message(0,3, "NTP Fail, HTTP Sync", true);
    triggerHttpTimeSync(); 
    return false;
}

void RTCManager::syncNITZ() {
    if (!_rtcOk) return;
    // DEBUG_PRINTLN(2, "Attempting NITZ/HTTP Time Sync...");
    _lcd_ref.message(0,2, "NITZ/HTTP Sync...", true);
    triggerHttpTimeSync(); // NITZ is often unreliable, go straight to HTTP
}

void RTCManager::triggerHttpTimeSync() {
    if (!_networkFacade.isConnected()) {
        // DEBUG_PRINTLN(1, "HTTP Time Sync: No network.");
        _lcd_ref.message(0,3, "HTTP Sync: No Net", true);
        return;
    }
    // DEBUG_PRINTLN(3, "Triggering Async HTTP Time Fetch...");
    _lcd_ref.message(0,3, "HTTP Time Fetch...", true);
    
    extern DeviceConfig deviceConfig; // Access global deviceConfig

    _networkFacade.startAsyncHttpRequest(deviceConfig.worldtime_url, "GET", "RTC_WT_ASYNC", nullptr,
        [this](JsonDocument& d) { // Capture this
            if (!d["unixtime"].isNull() && d["unixtime"].is<uint32_t>()) {
                uint32_t epoch = d["unixtime"].as<uint32_t>();
                this->adjustTime(epoch); 
                // DEBUG_PRINTF(3, "Async RTC WT CB: Epoch fetched & RTC adjusted: %lu\n", epoch);
                // _lcd_ref.message(0,3, "HTTP Time OK", true); // Message already in adjustTime
                return true;
            }
            // DEBUG_PRINTLN(1, F("Async RTC WT CB: Failed to parse unixtime."));
            this->_lcd_ref.message(0,3, "HTTP Time Parse ERR", true);
            return false;
        }, false);
}

String RTCManager::getFormattedDateTime() {
    if (_rtcOk) {
        DateTime n = _rtc.now();
        char dt_buffer[20]; // YYYY-MM-DD HH:MM:SS + null
        snprintf(dt_buffer, sizeof(dt_buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                 n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
        return String(dt_buffer);
    } else {
        return String("RTC Error"); // Indicate RTC problem
    }
}

bool RTCManager::isRtcOk() const {
    return _rtcOk;
}

bool RTCManager::_getNetworkDateTimeNTP(DateTime& dt) {
    // NetworkFacade* facade = dynamic_cast<NetworkFacade*>(&_networkFacade); // Not needed
    WiFiManager* wifiMgr = _networkFacade.getWiFiManager();
    if (wifiMgr && _networkFacade.getCurrentInterface() == wifiMgr && wifiMgr->isConnected()) {
        _timeClient.begin();
        _timeClient.setTimeOffset(NTP_TIMEZONE_OFFSET_SECONDS);
        if (_timeClient.forceUpdate()) {
            unsigned long e = _timeClient.getEpochTime();
            if (e > 1672531200UL) { 
                dt = DateTime(e);
                return true;
            }
        }
    }
    return false;
}