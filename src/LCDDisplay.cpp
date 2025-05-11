#include "LCDDisplay.h"
#include <stdio.h> // For snprintf
#include <string.h> // For strncpy

LCDDisplay::LCDDisplay() : _lcd_i2c(LCD_ADDR, 20, 4) {
    // Constructor body (if any needed besides initializer list)
}

void LCDDisplay::begin() {
    _lcd_i2c.init();
    _lcd_i2c.backlight();
    _lcd_i2c.setCursor(0, 0);
    _lcd_i2c.print(F("Relay Ctrl Loading.."));
}

void LCDDisplay::update(const char* dt, float temp, float hum, float light, bool r1, bool r2, bool r3, bool r4,
                        float tMin, float tMax, float humMin, float humMax, float lightMin, float lightMax,
                        bool netConnected, bool isDataStale, bool sdCardOkLocal, bool isInFailSafe) {
    char buf[21]; // Buffer for LCD line (20 chars + null)
    _lcd_i2c.clear();

    // Line 0: Status (Time, SD, Network)
    if (isInFailSafe) {
        snprintf(buf, sizeof(buf), "** FAILSAFE ** %-8s", dt + 11); // Show last 8 chars of datetime
    } else {
        const char* sdStatus = sdCardOkLocal ? "OK" : "!!";
        const char* netStatus = netConnected ? (isDataStale ? "STL" : "OFF") : "OFF";
        // Assuming dt is "YYYY-MM-DD HH:MM:SS", dt + 11 gives "HH:MM:SS"
        snprintf(buf, sizeof(buf), "%-8s SD:%-2s NW:%-3s", dt + 11, sdStatus, netStatus);
    }
    _lcd_i2c.setCursor(0, 0); 
    _lcd_i2c.print(buf);

    // Line 1: Sensor Data (Temp, Humidity, Light)
    snprintf(buf, sizeof(buf), "T:%.1fC H:%.0f%% L:%.0f", temp, hum, light);
    _lcd_i2c.setCursor(0, 1); 
    _lcd_i2c.print(buf);

    // Line 2: Relay Status
    snprintf(buf, sizeof(buf), "Exh:%c Deh:%c Blw:%c R4:%c",
             r1 ? 'Y' : 'N', r2 ? 'Y' : 'N', r3 ? 'Y' : 'N', r4 ? 'N' : 'N'); // Assuming R4 is always N
    _lcd_i2c.setCursor(0, 2); 
    _lcd_i2c.print(buf);

    // Line 3: Thresholds (Temp, Humidity)
    // Displaying general T and H thresholds. Blower uses T, Exhaust/Dehumidifier use H.
    char tempThresholdBuf[40]; // Larger buffer for intermediate formatting
    snprintf(tempThresholdBuf, sizeof(tempThresholdBuf), "T:%.0f-%.0f H:%.0f-%.0f", tMin, tMax, humMin, humMax);
    strncpy(buf, tempThresholdBuf, sizeof(buf) - 1); // Copy to LCD line buffer
    buf[sizeof(buf) - 1] = '\0'; // Ensure null termination
    _lcd_i2c.setCursor(0, 3); 
    _lcd_i2c.print(buf);
}

void LCDDisplay::message(int col, int row, const char* msg, bool clearLine) {
    if (clearLine) {
        _lcd_i2c.setCursor(0, row); 
        _lcd_i2c.print(F("                    ")); // 20 spaces
    }
    _lcd_i2c.setCursor(col, row);
    char buf[21];
    snprintf(buf, sizeof(buf), "%-20s", msg); // Pad with spaces to 20 chars
    _lcd_i2c.print(buf);
}

void LCDDisplay::clear() { 
    _lcd_i2c.clear(); 
}

void LCDDisplay::setCursor(int col, int row) { 
    _lcd_i2c.setCursor(col, row); 
}

void LCDDisplay::print(const char* msg) { 
    _lcd_i2c.print(msg); 
}

void LCDDisplay::print(const __FlashStringHelper* msg) { 
    _lcd_i2c.print(msg); 
}