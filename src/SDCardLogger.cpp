#include "SDCardLogger.h"
#include "config.h" // For DEBUG_PRINTLN, DEBUG_PRINTF
#include <SPI.h>   // For SPI.begin()
#include <stdio.h> // For snprintf
#include <string.h> // For strlen

// printDebugStatus was a global function from the .ino file, now removed.
// Using DEBUG_PRINTLN/F and LCD messages directly.

SDCardLogger::SDCardLogger(LCDDisplay* lcd) :
    _lcd(lcd),
    _sdCardOk(false) { // Initialize internal flag
    // Constructor
}

bool SDCardLogger::begin() {
    DEBUG_PRINTLN(3, "SDCardLogger: Initializing SD card...");
    _sdCardOk = false; // Assume failure until success

    // SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS); // Often called once in global setup
    // If begin() is called multiple times, ensure SPI pins are correctly set or SPI is re-initialized if necessary.
    // For robust re-initialization, SD.end() should be called first if already initialized.
    // However, this 'begin' is usually the first one.

    if (!SD.begin(SD_CS, SPI, 4000000)) { // Pass SPIClass instance and speed
        DEBUG_PRINTLN(1, "SDCardLogger: SD Mount Failed!");
        if (_lcd) _lcd->message(0, 3, "SD Mount Fail!", true);
        _sdCardOk = false;
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        DEBUG_PRINTLN(1, "SDCardLogger: No SD Card found!");
        if (_lcd) _lcd->message(0, 3, "No SD Card!", true);
        SD.end(); // Release SPI bus
        _sdCardOk = false;
        return false;
    }
    DEBUG_PRINTF(3, "SDCardLogger: SD Card OK. Type: %d, Size: %lluMB\n", SD.cardType(), SD.cardSize() / (1024 * 1024));
    if (_lcd) _lcd->message(0, 3, "SD Card OK", true);
    _sdCardOk = true;
    return true;
}

bool SDCardLogger::reInit() {
    DEBUG_PRINTLN(2, "SDCardLogger: Re-initializing SD card...");
    SD.end(); // End current SD session
    delay(100); // Short delay before re-trying
    return begin(); // Call the main begin function
}

void SDCardLogger::logData(const char* dateTime, 
                           float temp, float hum, float light,
                           float tempMin, float tempMax, 
                           float humMin, float humMax, 
                           float lightMin, float lightMax,
                           bool r1, bool r2, bool r3, bool r4) {
    if (!_sdCardOk) {
        DEBUG_PRINTLN(2, "SDCardLogger: Log attempt while SD not OK.");
        return;
    }

    File lf = SD.open("/log.csv", FILE_APPEND);
    if (!lf) {
        DEBUG_PRINTLN(1, "SDCardLogger: Failed to open log.csv for append. Attempting re-init...");
        if (reInit()) { // Try to re-initialize SD card
            lf = SD.open("/log.csv", FILE_APPEND); // Try opening again
            if (!lf) {
                DEBUG_PRINTLN(1, "SDCardLogger: Still failed to open log.csv after re-init.");
                _sdCardOk = false; // Mark SD as not OK
                return;
            }
        } else {
            DEBUG_PRINTLN(1, "SDCardLogger: SD re-init failed during log attempt.");
            _sdCardOk = false; // Mark SD as not OK
            return;
        }
    }

    if (lf.size() == 0) { // If file is new or empty, write header
        lf.println(F("DateTime,Temperature,Humidity,Light,TempMin,TempMax,HumMin,HumMax,LightMin,LightMax,Relay1,Relay2,Relay3,Relay4"));
    }

    char ln[250]; // Buffer for log line
    snprintf(ln, sizeof(ln), "%s,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%s,%s,%s",
             dateTime, temp, hum, light,
             tempMin, tempMax, humMin, humMax, lightMin, lightMax,
             r1 ? "ON" : "OFF", r2 ? "ON" : "OFF", r3 ? "ON" : "OFF", r4 ? "ON" : "OFF");
    
    size_t bytesWritten = lf.println(ln);
    if (bytesWritten < strlen(ln)) { // Check if less than expected, not just not equal to strlen + 2, as println might handle \r\n differently or not at all on some platforms. More robust to check if basic string length was written.
        DEBUG_PRINTLN(1, "SDCardLogger: Error writing data to log.csv or disk full.");
        _sdCardOk = false; // A write error is serious
    }
    lf.close();
}

bool SDCardLogger::isSdCardOk() const {
    return _sdCardOk;
}

void SDCardLogger::logEvent(const char* dateTime, const char* eventMessage) {
    if (!_sdCardOk) {
        DEBUG_PRINTLN(2, "SDCardLogger: Log event attempt while SD not OK.");
        return;
    }

    File ef = SD.open("/events.txt", FILE_APPEND);
    if (!ef) {
        DEBUG_PRINTLN(1, "SDCardLogger: Failed to open events.txt for append. Attempting re-init...");
        if (reInit()) { // Try to re-initialize SD card
            ef = SD.open("/events.txt", FILE_APPEND); // Try opening again
            if (!ef) {
                DEBUG_PRINTLN(1, "SDCardLogger: Still failed to open events.txt after re-init.");
                _sdCardOk = false; // Mark SD as not OK
                return;
            }
        } else {
            DEBUG_PRINTLN(1, "SDCardLogger: SD re-init failed during event log attempt.");
            _sdCardOk = false; // Mark SD as not OK
            return;
        }
    }

    char ln[200]; // Buffer for event log line
    snprintf(ln, sizeof(ln), "%s - %s", dateTime, eventMessage);
    
    size_t bytesWritten = ef.println(ln);
    if (bytesWritten < strlen(ln)) { // Check if less than expected
        DEBUG_PRINTLN(1, "SDCardLogger: Error writing event to events.txt or disk full.");
        _sdCardOk = false; // A write error is serious
    }
    ef.close();
}