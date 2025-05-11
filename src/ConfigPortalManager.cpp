#include "ConfigPortalManager.h"
#include "config.h" // For DEBUG_PRINTLN_F, MODEM_POWER_ON, PORTAL_TIMEOUT etc.
#include "WiFiManager.h"  // For WiFiManager class definition
#include "GPRSManager.h"  // For GPRSManager class definition
#include <WiFi.h>
#include <esp_task_wdt.h> // For esp_task_wdt_reset()

// Define the static CONFIG_PAGE (Copied from Project2R_GH1_exp.ino)
const char ConfigPortalManager::CONFIG_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><title>ESP32 Relay Config</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif; padding: 10px;}label{display: block;margin-top:10px;font-weight:bold;}input[type='text'],input[type='password'],select{width:95%;max-width:400px;padding:8px; margin-top: 5px; border: 1px solid #ccc; border-radius: 4px;}input[type='submit'], button{background-color: #4CAF50; color: white; padding:12px 20px; border: none; border-radius: 4px; cursor: pointer; margin-top:20px; font-size: 1em;} input[type='submit']:hover, button:hover{background-color: #45a049;} .note{font-size: 0.8em; color: #555; margin-top: 5px;} .button-secondary{background-color: #f44336;} .button-secondary:hover{background-color: #da190b;}</style></head><body><h1>ESP32 Relay Config</h1><p>Configure device settings, WiFi network, and API token.</p><form method='POST' action='/save'><h2>Device Settings</h2><label for='gh_id'>Greenhouse ID:</label><select id='gh_id' name='gh_id'><option value='1' %GH_ID_1_SELECTED%>Greenhouse 1</option><option value='2' %GH_ID_2_SELECTED%>Greenhouse 2</option></select><h2>WiFi Settings</h2><label for='ssid'>WiFi SSID:</label><input type='text' id='ssid' name='ssid' value='%SSID%' required><label for='pass'>WiFi Password:</label><input type='password' id='pass' name='pass' value='%PASS%' placeholder='Leave blank to keep current'><div class='note'>Leave password blank to keep the existing one saved in NVS.</div><h2>API Settings</h2><label for='token'>API Auth Token:</label><input type='text' id='token' name='token' value='%TOKEN%' required><div class='note'>API URLs for Thresholds and Node Data are automatically generated based on the Greenhouse ID.</div><br><input type='submit' value='Save & Restart'></form><hr style='margin-top: 30px; margin-bottom: 20px;'><form method='GET' action='/factoryreset' onsubmit='return confirm("Are you sure you want to perform a factory reset? All settings will be lost.");'><button type='submit' class='button-secondary'>Factory Reset & Restart</button></form></body></html>
)=====";

ConfigPortalManager::ConfigPortalManager(DeviceConfig& config, LCDDisplay& lcd, NetworkFacade* networkFacade)
    : _deviceConfig(config), _lcd(lcd), _networkFacade(networkFacade), _server(80) {
    // Constructor body, if needed for other initializations
}

ConfigPortalManager::~ConfigPortalManager() {
    _server.stop();
    _dnsServer.stop();
}

String ConfigPortalManager::processor(const String& var) {
    if (var == "SSID") return String(_deviceConfig.ssid);
    if (var == "PASS") return ""; // Always return empty for password field placeholder
    if (var == "TOKEN") return String(_deviceConfig.api_token);
    if (var == "GH_ID_1_SELECTED") return (_deviceConfig.gh_id == 1) ? "selected" : "";
    if (var == "GH_ID_2_SELECTED") return (_deviceConfig.gh_id == 2) ? "selected" : "";
    return String();
}

bool ConfigPortalManager::handleCaptivePortal() {
    String hostHeader = _server.hostHeader();
    const char* host_cstr = hostHeader.c_str();
    IPAddress softAPIP = WiFi.softAPIP();
    String softAPIPStr = softAPIP.toString();
    const char* ap_ip_cstr = softAPIPStr.c_str();

    if (strcmp(host_cstr, ap_ip_cstr) != 0 &&
        (strchr(host_cstr, '.') != nullptr || strcmp(host_cstr, "localhost") == 0)) {
        char location_buffer[40];
        snprintf(location_buffer, sizeof(location_buffer), "http://%s", ap_ip_cstr);
        _server.sendHeader("Location", location_buffer, true);
        _server.send(302, "text/plain", "");
        return true;
    }

    String requestUri = _server.uri();
    const char* uri_cstr = requestUri.c_str();

    if (strstr(uri_cstr, "generate_204") != nullptr ||
        strstr(uri_cstr, "success.html") != nullptr ||
        strstr(uri_cstr, "check_network_status.txt") != nullptr ||
        strstr(uri_cstr, "ncsi.txt") != nullptr ||
        strstr(uri_cstr, "hotspot-detect.html") != nullptr) {
        _server.send(204, "text/plain", "");
        return true;
    }
    return false;
}

void ConfigPortalManager::handleRoot() {
    String pC = FPSTR(CONFIG_PAGE);
    pC.replace("%GH_ID_1_SELECTED%", processor("GH_ID_1_SELECTED"));
    pC.replace("%GH_ID_2_SELECTED%", processor("GH_ID_2_SELECTED"));
    pC.replace("%SSID%", processor("SSID"));
    pC.replace("%PASS%", processor("PASS")); // Stays empty
    pC.replace("%TOKEN%", processor("TOKEN"));
    _server.send(200, "text/html", pC);
}

void ConfigPortalManager::handleSave() {
    String nS = _server.arg("ssid");
    String nP = _server.arg("pass");
    String nT = _server.arg("token");
    String gh_id_str = _server.arg("gh_id");

    if (nS.length() == 0 || nT.length() == 0 || gh_id_str.length() == 0) {
        _server.send(400, "text/plain", F("Bad Request: SSID, Token, and GH ID are required."));
        return;
    }
    int new_gh_id = gh_id_str.toInt();
    if (new_gh_id != 1 && new_gh_id != 2) {
        _server.send(400, "text/plain", F("Bad Request: Invalid GH ID."));
        return;
    }

    const char* final_pwd_for_save;
    if (nP.length() > 0) {
        final_pwd_for_save = nP.c_str();
    } else {
        final_pwd_for_save = _deviceConfig.password;
    }

    bool svd = _deviceConfig.saveConfig(new_gh_id, nS.c_str(), final_pwd_for_save, nT.c_str());

    if (svd && _networkFacade) {
        if (_networkFacade->getWiFiManager()) {
            _networkFacade->getWiFiManager()->setCredentials(_deviceConfig.ssid, _deviceConfig.password);
            _networkFacade->getWiFiManager()->setAuthToken(_deviceConfig.api_token);
        }
        if (_networkFacade->getGPRSManager()) {
            _networkFacade->getGPRSManager()->setAuthToken(_deviceConfig.api_token);
        }
    }

    char response_msg_buffer[256];
    const char* save_status_text = svd ? "Configuration Saved!" : "Error Saving Configuration!";
    snprintf(response_msg_buffer, sizeof(response_msg_buffer),
             "<html><head><title>Save Configuration</title><meta http-equiv='refresh' content='3;url=/'></head><body><h1>%s</h1><p>Device will restart in 3 seconds...</p></body></html>",
             save_status_text);
    _server.send(200, "text/html", response_msg_buffer);
    delay(3000);
    ESP.restart();
}

void ConfigPortalManager::handleFactoryReset() {
    DEBUG_PRINTLN_F(1, F("Factory reset requested via ConfigPortalManager."));
    _lcd.clear();
    _lcd.message(0, 0, "FACTORY RESET...", true);

    _deviceConfig.factoryResetConfig(); // factoryResetConfig is void, cannot assign to bool

    char response_msg_buffer[256];
    // Since factoryResetConfig is void, we assume success for the message.
    // Error handling within factoryResetConfig should log errors internally if any.
    // The GH_ID will be reloaded from defaults by factoryResetConfig itself.
    const char* reset_status_text = "Factory Reset Initiated!";
    
    snprintf(response_msg_buffer, sizeof(response_msg_buffer),
             "<html><head><title>Factory Reset</title><meta http-equiv='refresh' content='5;url=/'></head><body><h1>%s (GH_ID: %d)</h1><p>All settings have been reset to defaults.</p><p>Device will restart in 5 seconds...</p></body></html>",
             reset_status_text, _deviceConfig.gh_id);
    _server.send(200, "text/html", response_msg_buffer);
    
    DEBUG_PRINTLN_F(1, F("Device restarting after factory reset (ConfigPortalManager)..."));
    delay(5000);
    ESP.restart();
}

void ConfigPortalManager::handleNotFound() {
    if (!handleCaptivePortal()) {
        _server.send(404, "text/plain", F("Not found"));
    }
}

bool ConfigPortalManager::startPortal() {
    DEBUG_PRINTLN_F(1, F("Starting Config Portal (ConfigPortalManager)..."));
    _lcd.clear();
    _lcd.message(0, 0, "CONFIG PORTAL MODE", false);
    
    WiFi.disconnect(true); 
    delay(100); 
    // Consider if MODEM_POWER_ON manipulation is specific enough to be here
    // or should be handled by a higher-level system (e.g. before calling startPortal)
    // For now, keeping it as it was in the original .ino file.
    pinMode(MODEM_POWER_ON, OUTPUT); 
    digitalWrite(MODEM_POWER_ON, LOW);

    WiFi.mode(WIFI_AP);

    // Create a unique AP SSID using part of the MAC address
    char apS_unique[32]; // Buffer for "GH_Portal_XXYYZZ"
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apS_unique, sizeof(apS_unique), "GH_Portal_%02X%02X%02X", mac[3], mac[4], mac[5]);
    
    const char* apP = "password123"; // Generic AP Password, ensure it meets complexity if any required by WiFi library (e.g. min 8 chars)
    
    _lcd.message(0, 1, "SSID:", false);
    // Ensure the unique SSID fits on the LCD line, or truncate if necessary
    char lcd_ssid_display[17]; // Max 16 chars + null for typical 16-char line
    strncpy(lcd_ssid_display, apS_unique, 16);
    lcd_ssid_display[16] = '\0'; // Ensure null termination
    _lcd.message(6, 1, lcd_ssid_display, false); // Display potentially truncated SSID
    _lcd.message(0, 2, "PWD: ", false);
    _lcd.message(5, 2, "password123", false); // Show the actual password for clarity during setup

    if (!WiFi.softAP(apS_unique, apP)) {
        _lcd.message(0, 3, "AP START FAILED!", true);
        delay(5000);
        ESP.restart();
        return false; // Should not reach here
    }
    delay(500); // Wait for AP to be fully up

    IPAddress ip = WiFi.softAPIP();
    _lcd.message(0, 3, "IP: ", false);
    _lcd.message(4, 3, ip.toString().c_str(), false);

    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    if (!_dnsServer.start(53, "*", ip)) {
        _lcd.message(0, 3, "DNS FAILED!", true); // Overwrites IP on LCD line 3
        // Potentially log or handle this failure more gracefully than just showing on LCD
    }

    _server.on("/", HTTP_GET, std::bind(&ConfigPortalManager::handleRoot, this));
    _server.on("/save", HTTP_POST, std::bind(&ConfigPortalManager::handleSave, this));
    _server.on("/factoryreset", HTTP_GET, std::bind(&ConfigPortalManager::handleFactoryReset, this));
    _server.onNotFound(std::bind(&ConfigPortalManager::handleNotFound, this));
    
    _server.begin();
    DEBUG_PRINTLN_F(2, F("Config Portal Server Started. Waiting for client or timeout..."));

    unsigned long portalStartTime = millis();
    while (millis() - portalStartTime < PORTAL_TIMEOUT) {
        esp_task_wdt_reset();
        _dnsServer.processNextRequest();
        _server.handleClient();
        yield(); // Allow background tasks
        // Check if a configuration has been saved (e.g., via a flag set in handleSave)
        // For now, relies on ESP.restart() within handlers or timeout.
        // If we want startPortal to return true/false based on save, need a member flag.
    }

    _lcd.clear();
    _lcd.message(0, 0, "Portal Timeout", true);
    DEBUG_PRINTLN_F(1, F("Config Portal timed out. Restarting."));
    delay(2000);
    ESP.restart();
    return false; // Should not reach here due to ESP.restart()
}