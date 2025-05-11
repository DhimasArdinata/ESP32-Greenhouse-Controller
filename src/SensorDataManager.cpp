#include "SensorDataManager.h"
#include "config.h" // For DEBUG_PRINTLN and DEBUG_LEVEL

SensorDataManager::SensorDataManager() :
    temperature(-99.9), humidity(-1.0), light(-1.0), // Default invalid values
    tempMin(25.0), tempMax(30.0),   // Default thresholds
    humMin(60.0), humMax(80.0),
    lightMin(500.0), lightMax(5000.0) {
    // Constructor body
}

void SensorDataManager::updateThresholds(float tMin, float tMax, float hMin, float hMax, float lMin, float lMax) {
    if (tMin < 80.0 && tMax > -20.0 && tMin <= tMax) { 
        tempMin = tMin; tempMax = tMax; 
    } else { 
        DEBUG_PRINTLN(1, "Err: Invalid temp thresh"); 
    }
    if (hMin >= 0.0 && hMax <= 100.0 && hMin <= hMax) { 
        humMin = hMin; humMax = hMax; 
    } else { 
        DEBUG_PRINTLN(1, "Err: Invalid hum thresh"); 
    }
    if (lMin >= 0.0 && lMax > 0.0 && lMin <= lMax) { // Assuming lightMax must be > 0
        lightMin = lMin; lightMax = lMax; 
    } else { 
        DEBUG_PRINTLN(1, "Err: Invalid light thresh"); 
    }
    // Using DEBUG_PRINTF for better memory management if available and configured
    DEBUG_PRINTF(3, "Thresh updated: T:%.1f-%.1f H:%.0f-%.0f L:%.0f-%.0f\n", tempMin, tempMax, humMin, humMax, lightMin, lightMax);
}

void SensorDataManager::updateData(float temp, float hum, float lgt) {
    if (temp > -40.0 && temp < 100.0) { temperature = temp; } else { temperature = -99.9; }
    if (hum >= 0.0 && hum <= 100.0) { humidity = hum; } else { humidity = -1.0; }
    if (lgt >= 0.0 && lgt < 100000.0) { light = lgt; } else { light = -1.0; } // Assuming a reasonable upper limit for light
    DEBUG_PRINTF(3, "Sensor data: T=%.1f H=%.0f L=%.0f\n", temperature, humidity, light);
}

bool SensorDataManager::loadFromLog() {
    DEBUG_PRINTLN(3, "SensorDataManager: Loading from log...");
    if (!SD.cardType() || SD.cardType() == CARD_NONE) { // Check if SD card is present
        DEBUG_PRINTLN(1, "Err: No SD card for log load.");
        return false;
    }
    File f = SD.open("/log.csv", FILE_READ);
    if (!f) { 
        DEBUG_PRINTLN(1, "Err: Log open fail"); 
        return false; 
    }
    if (!f.size()) { 
        DEBUG_PRINTLN(2, "Warn: Log empty"); 
        f.close(); 
        return false; 
    }

    String lastLine = ""; 
    long fileSize = f.size();
    long position = fileSize - 1; 
    int charsRead = 0;
    const int maxCharsToRead = 512; // Safety break for reading last line

    // Seek backwards from end of file to find the start of the last line
    while (position > 0 && charsRead < maxCharsToRead) {
        f.seek(position);
        char c = f.read();
        if (c == '\n') {
            if (lastLine.length() > 0) break; // Found start of previous line, current lastLine is complete
        } else if (c != '\r') {
            lastLine = c + lastLine; // Prepend char to build line in correct order
        }
        position--;
        charsRead++;
    }
    // If loop finished due to position=0 or maxCharsToRead, and lastLine is still empty,
    // it might be a single-line file. Read from beginning.
    if (lastLine.length() == 0 && fileSize > 0 && f.available()) {
        f.seek(0);
        lastLine = f.readStringUntil('\n');
        lastLine.trim();
    }
    
    f.close();

    if (lastLine.length() == 0 || lastLine.startsWith(F("DateTime,"))) { 
        DEBUG_PRINTLN(2, "Warn: Last log line invalid or header."); 
        return false; 
    }

    // Parse the CSV: DateTime,Temp,Hum,Light,TMin,TMax,HMin,HMax,LMin,LMax,R1,R2,R3,R4
    // We are interested in fields 1-9 (0-indexed after split)
    int commaIndices[14]; // Max 13 commas for 14 fields
    commaIndices[0] = -1; // Start before the first char
    int fieldCount = 0;

    for(int i = 1; i < 14; ++i) {
        commaIndices[i] = lastLine.indexOf(',', commaIndices[i-1] + 1);
        if (commaIndices[i] == -1 && i < 13) { // If a comma is missing before the last field
            DEBUG_PRINTLN(1,"Err: Log parse fields - not enough commas."); 
            return false;
        }
        fieldCount = i;
        if (commaIndices[i] == -1) break; // Stop if no more commas (last field)
    }
    if (fieldCount < 10) { // Need at least up to LightMax (index 9)
         DEBUG_PRINTLN(1,"Err: Log parse fields - too few fields.");
         return false;
    }


    try {
        // DateTime is commaIndices[0]+1 to commaIndices[1]
        temperature = lastLine.substring(commaIndices[1] + 1, commaIndices[2]).toFloat();
        humidity    = lastLine.substring(commaIndices[2] + 1, commaIndices[3]).toFloat();
        light       = lastLine.substring(commaIndices[3] + 1, commaIndices[4]).toFloat();
        tempMin     = lastLine.substring(commaIndices[4] + 1, commaIndices[5]).toFloat();
        tempMax     = lastLine.substring(commaIndices[5] + 1, commaIndices[6]).toFloat();
        humMin      = lastLine.substring(commaIndices[6] + 1, commaIndices[7]).toFloat();
        humMax      = lastLine.substring(commaIndices[7] + 1, commaIndices[8]).toFloat();
        lightMin    = lastLine.substring(commaIndices[8] + 1, commaIndices[9]).toFloat();
        // For the last field (lightMax), if commaIndices[10] is -1, it means it's the end of the string
        int endIdxLightMax = (commaIndices[10] == -1) ? lastLine.length() : commaIndices[10];
        lightMax    = lastLine.substring(commaIndices[9] + 1, endIdxLightMax).toFloat();
        
        DEBUG_PRINTLN(3, "Log loaded successfully into SensorDataManager."); 
        // Optionally, update current sensor values if they are valid from log
        updateData(temperature, humidity, light); 
        return true;
    } catch(...) { // Generic catch, consider more specific error handling if possible
        DEBUG_PRINTLN(1, "Err: Log parse exception during conversion."); 
        return false; 
    }
}

// Getter methods
float SensorDataManager::getTempMin() const { return tempMin; }
float SensorDataManager::getTempMax() const { return tempMax; }
float SensorDataManager::getHumMin() const { return humMin; }
float SensorDataManager::getHumMax() const { return humMax; }
float SensorDataManager::getLightMin() const { return lightMin; }
float SensorDataManager::getLightMax() const { return lightMax; }