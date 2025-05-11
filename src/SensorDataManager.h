/**
 * @file SensorDataManager.h
 * @brief Defines the `SensorDataManager` class, a central repository for current environmental
 *        sensor readings (temperature, humidity, light) and their operational thresholds.
 *
 * This file declares the `SensorDataManager` class. Its primary role is to encapsulate
 * and manage the state of various environmental sensors and the corresponding minimum/maximum
 * thresholds that define desired operational ranges. This data is crucial for other system
 * components, such as the `RelayController`, to make control decisions.
 *
 * Key functionalities include:
 * - Data Storage: Holds public member variables for current temperature (`temperature`),
 *   humidity (`humidity`), and light (`light`) readings. It also stores the configured
 *   minimum and maximum thresholds for each of these: `tempMin`, `tempMax`, `humMin`,
 *   `humMax`, `lightMin`, and `lightMax`.
 * - Initialization: The constructor (`SensorDataManager()`) sets initial default values
 *   for sensor readings (often indicating "not yet read" or a safe default) and thresholds
 *   (representing default operational ranges, possibly overridden later).
 * - Data Update:
 *   - `updateData()`: Allows updating the current sensor readings with new values obtained
 *     from physical sensors.
 *   - `updateThresholds()`: Allows modification of the operational thresholds, likely based
 *     on user configuration or system settings.
 *   Both methods perform basic validation on the input values.
 * - Persistence (`loadFromLog()`): Provides a mechanism to initialize or restore sensor
 *   readings and threshold values by parsing them from the last entry in a log file stored
 *   on an SD card. This allows the system to resume with previously known states.
 *   This functionality relies on `<FS.h>` and `<SD.h>` for file system operations.
 * - Access: Provides getter methods (`getTempMin()`, `getTempMax()`, etc.) for retrieving
 *   the configured thresholds. Current sensor values are accessed directly via public members.
 *
 * This class centralizes sensor-related data, making it easily accessible and manageable
 * throughout the application.
 */
#ifndef SENSOR_DATA_MANAGER_H
#define SENSOR_DATA_MANAGER_H

#include <Arduino.h> // Core Arduino framework, provides basic types (float, bool) and functions.
#include <FS.h>      // Filesystem library (part of ESP32/Arduino core), used for File operations,
                     // specifically for `loadFromLog()` when reading from the SD card.
#include <SD.h>      // SD card library, necessary for interacting with the SD card,
                     // particularly for `SD.open()` in `loadFromLog()`.

// Note on config.h: DEBUG_PRINTLN and other debugging macros, if used by SensorDataManager,
// would typically be handled in the SensorDataManager.cpp implementation file. If SensorDataManager.h
// itself required them (e.g., for inline methods with debugging output, though none exist here),
// then "config.h" would be included.

/**
 * @class SensorDataManager
 * @brief Manages and stores current environmental sensor readings and their operational thresholds.
 *
 * This class acts as a data container for:
 * - Current temperature, humidity, and light sensor readings.
 * - Configured minimum and maximum thresholds for temperature, humidity, and light.
 *
 * It provides methods to initialize these values (constructor, `loadFromLog()`),
 * update them (`updateData()`, `updateThresholds()`), and access the thresholds (`getTempMin()`, etc.).
 * The actual sensor readings are public members for direct access.
 * The `loadFromLog()` method enables persistence by reading the last known state from an SD card.
 */
class SensorDataManager {
public:
    // --- Current Sensor Value Members ---
    /**
     * @brief Current ambient temperature reading.
     * Units are typically degrees Celsius (°C) but depend on the sensor and application.
     * Updated via `updateData()` or `loadFromLog()`.
     */
    float temperature;
    /**
     * @brief Current ambient humidity reading.
     * Units are typically relative humidity percentage (%RH).
     * Updated via `updateData()` or `loadFromLog()`.
     */
    float humidity;
    /**
     * @brief Current ambient light intensity reading.
     * Units can vary (e.g., lux, raw ADC value) depending on the sensor.
     * Updated via `updateData()` or `loadFromLog()`.
     */
    float light;

    // --- Operational Threshold Members ---
    /** @brief Minimum acceptable temperature threshold. Used for control decisions. */
    float tempMin;
    /** @brief Maximum acceptable temperature threshold. Used for control decisions. */
    float tempMax;
    /** @brief Minimum acceptable humidity threshold. Used for control decisions. */
    float humMin;
    /** @brief Maximum acceptable humidity threshold. Used for control decisions. */
    float humMax;
    /** @brief Minimum acceptable light intensity threshold. Used for control decisions. */
    float lightMin;
    /** @brief Maximum acceptable light intensity threshold. Used for control decisions. */
    float lightMax;

    /**
     * @brief Constructor for `SensorDataManager`.
     * Initializes all sensor reading members (e.g., `temperature`, `humidity`, `light`)
     * to default values, often representing an "invalid" or "not yet read" state (e.g., 0.0 or -1.0).
     * Initializes all threshold members (e.g., `tempMin`, `tempMax`) to default operational
     * ranges, which might be broad fallbacks if not subsequently updated or loaded.
     */
    SensorDataManager();

    /**
     * @brief Updates all configured operational thresholds for temperature, humidity, and light.
     * This method allows for dynamic adjustment of the control boundaries.
     * It performs basic validation (e.g., ensuring min <= max) before applying the new values.
     *
     * @param tMin New minimum temperature threshold.
     * @param tMax New maximum temperature threshold.
     * @param hMin New minimum humidity threshold.
     * @param hMax New maximum humidity threshold.
     * @param lMin New minimum light intensity threshold.
     * @param lMax New maximum light intensity threshold.
     */
    void updateThresholds(float tMin, float tMax, float hMin, float hMax, float lMin, float lMax);

    /**
     * @brief Updates the current sensor data readings for temperature, humidity, and light.
     * This method is called when new data is available from the physical sensors.
     * It performs basic validation on the incoming values (e.g., checking for NaN or extreme values,
     * though specific validation logic is in the `.cpp` file).
     *
     * @param temp New temperature reading from the sensor.
     * @param hum New humidity reading from the sensor.
     * @param lgt New light intensity reading from the sensor.
     */
    void updateData(float temp, float hum, float lgt);

    /**
     * @brief Attempts to load the last known sensor data readings and operational thresholds
     *        from a specified log file on the SD card (e.g., `DATA_LOG_FILENAME` from `config.h`).
     *
     * This function reads the last valid line from the log file, parses it to extract
     * temperature, humidity, light, and their respective min/max thresholds, and then
     * updates the corresponding member variables of this `SensorDataManager` instance.
     * This is useful for restoring the system to a previously known state on startup.
     * Uses `SD.open()` and `File` objects for SD card interaction.
     *
     * @return `true` if data was successfully found, read, parsed from the log file,
     *         and applied to the current instance.
     * @return `false` if the SD card cannot be initialized, the log file cannot be opened,
     *         the file is empty, or the data format is invalid.
     */
    bool loadFromLog();

    // --- Getter Methods for Thresholds ---
    // These provide controlled read-only access to the threshold values.

    /** @brief Gets the configured minimum temperature threshold. @return The minimum temperature threshold. */
    float getTempMin() const;
    /** @brief Gets the configured maximum temperature threshold. @return The maximum temperature threshold. */
    float getTempMax() const;
    /** @brief Gets the configured minimum humidity threshold. @return The minimum humidity threshold. */
    float getHumMin() const;
    /** @brief Gets the configured maximum humidity threshold. @return The maximum humidity threshold. */
    float getHumMax() const;
    /** @brief Gets the configured minimum light intensity threshold. @return The minimum light intensity threshold. */
    float getLightMin() const;
    /** @brief Gets the configured maximum light intensity threshold. @return The maximum light intensity threshold. */
    float getLightMax() const;
};

#endif // SENSOR_DATA_MANAGER_H