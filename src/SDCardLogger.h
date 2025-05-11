/**
 * @file SDCardLogger.h
 * @brief Defines the `SDCardLogger` class for managing data and event logging to an SD card.
 *
 * This file declares the `SDCardLogger` class, which encapsulates all functionality
 * related to interacting with an SD card using the standard SD library via SPI.
 * Key responsibilities include:
 * - Initialization: Setting up SPI communication with the SD card (using `SD_CS_PIN` from
 *   `config.h`) and mounting the filesystem. This is handled by the `begin()` method.
 * - Status Checking: Providing a way to check if the SD card is currently initialized
 *   and considered operational (`isSdCardOk()`).
 * - Re-initialization: Offering a mechanism to attempt re-initializing the SD card
 *   if it becomes unresponsive (`reInit()`). This is also automatically attempted by
 *   logging methods upon encountering write errors.
 * - Structured Data Logging: Writing comprehensive sensor readings, configured thresholds,
 *   and relay states to a CSV file. The filename for this log is typically defined by
 *   `LOG_FILENAME` in `config.h` (e.g., "/log.csv"). If the file doesn't exist or is empty,
 *   a header row is automatically added.
 * - Event Logging: Writing timestamped system events or general messages to a separate
 *   plain text file. The filename for this event log is typically `EVENT_LOG_FILENAME` in
 *   `config.h` (e.g., "/event_log.txt").
 *
 * The class optionally interacts with an `LCDDisplay` object (passed during construction)
 * to provide visual feedback to the user regarding SD card operations (e.g., initialization
 * success/failure, logging errors). Debugging output to the Serial monitor is controlled
 * by macros like `DEBUG_PRINTLN` from `config.h`.
 */
#ifndef SDCARD_LOGGER_H
#define SDCARD_LOGGER_H

#include <Arduino.h>     // Core Arduino framework (String, `millis()`, etc.).
#include <FS.h>          // ESP32/ESP8266 Filesystem library, providing the `File` object and `FS` interface.
#include <SD.h>          // Arduino SD card library for SPI communication with the card.
#include "LCDDisplay.h"  // For displaying status messages and errors related to SD card operations.
#include "config.h"      // Essential for `SD_CS_PIN`, `LOG_FILENAME`, `EVENT_LOG_FILENAME`,
                         // `DEBUG_PRINTLN`, and other related configurations.
 
// Note: Any previous global debug functions like printDebugStatus are likely
// deprecated or integrated. Debugging is primarily handled via `DEBUG_PRINTLN`/`DEBUG_PRINTF`
// macros from `config.h` and through messages displayed on the LCD.
 
/**
 * @class SDCardLogger
 * @brief Manages all aspects of data and event logging to an SD card.
 *
 * This class provides a high-level interface for SD card operations, abstracting
 * the low-level details of SPI communication, filesystem mounting, file handling
 * (opening, creating, writing, closing), and error management.
 * A key feature is its resilience: if a logging operation (to data or event file)
 * fails, it automatically attempts to `reInit()` the SD card once and then retries
 * the operation, aiming to recover from transient SD card issues.
 */
class SDCardLogger {
public:
    /**
     * @brief Constructor for `SDCardLogger`.
     * Initializes the logger with an optional `LCDDisplay` object.
     * Sets the initial SD card status (`_sdCardOk`) to `false`.
     *
     * @param lcd Pointer to an `LCDDisplay` object. This is used to display status
     *            messages (e.g., "SD Init OK", "SD Log Error", "SD Reinit Fail") and provide visual
     *            feedback to the user regarding SD card operations. If `nullptr` is passed,
     *            no LCD output will be attempted by this logger.
     */
    SDCardLogger(LCDDisplay* lcd);

    /**
     * @brief Initializes the SD card and prepares it for logging.
     * This method must be called once (typically in `setup()`) before any other SD card
     * operations can be performed. It attempts to:
     * 1. Initialize the SPI communication with the SD card using `SD.begin(SD_CS_PIN)`,
     *    where `SD_CS_PIN` is defined in `config.h`.
     * 2. Mount the SD card filesystem.
     *
     * It updates the internal `_sdCardOk` status flag based on the success or failure of
     * these operations. Messages indicating the outcome (e.g., "SD Init OK", "SD Init FAIL")
     * are displayed on the `_lcd` (if provided) and printed to Serial (if `DEBUG_MODE_SDCARD` is enabled).
     *
     * @return `true` if SD card initialization is successful and the card is ready for use.
     * @return `false` if initialization fails (e.g., card not present, SPI communication error,
     *         filesystem mount failure).
     */
    bool begin();

    /**
     * @brief Attempts to re-initialize the SD card.
     * This method can be called if the SD card becomes unresponsive or if an error occurs
     * during a file operation. It performs the following steps:
     * 1. Calls `SD.end()` to release SPI resources and unmount the filesystem.
     * 2. Calls `begin()` to re-attempt the full initialization sequence.
     *
     * It updates the internal `_sdCardOk` status flag. Messages about the re-initialization
     * attempt and its outcome are displayed on the `_lcd` and Serial.
     *
     * @return `true` if re-initialization is successful and the card is ready.
     * @return `false` if re-initialization fails.
     */
    bool reInit();

    /**
     * @brief Checks if the SD card is currently initialized and deemed operational.
     *
     * @return `true` if the `_sdCardOk` flag is `true`. This means that the last call to
     *         `begin()` or `reInit()` was successful and no critical, unrecoverable
     *         errors have occurred since that would have set `_sdCardOk` to `false`.
     * @return `false` if the SD card is not initialized or an error state has been recorded
     *         (e.g., multiple failed re-initialization attempts).
     */
    bool isSdCardOk() const;

    /**
     * @brief Logs a comprehensive set of sensor data, configuration thresholds, and current relay states
     * to a CSV (Comma Separated Values) file on the SD card.
     *
     * The target filename is specified by `LOG_FILENAME` in `config.h` (e.g., "/log.csv").
     * If the log file does not exist or is empty upon the first call (or after being cleared),
     * a header row is automatically written to define the columns. The standard header is:
     * `"DateTime,Temp,Humidity,Light,TempMin,TempMax,HumMin,HumMax,LightMin,LightMax,R1,R2,R3,R4"`
     * Subsequent calls append a new row of data.
     *
     * Error Handling:
     * If opening or writing to the log file fails, this method will automatically attempt to
     * `reInit()` the SD card once. If the re-initialization is successful, it will retry the
     * logging operation. If re-initialization fails or the retry also fails, an error message
     * is displayed on the `_lcd` and printed to Serial, and `_sdCardOk` might be set to `false`.
     *
     * @param dateTime A C-style string representing the current date and time when the data was recorded
     *                 (e.g., "YYYY-MM-DD HH:MM:SS" format from an RTC).
     * @param temp Current ambient temperature reading (float, e.g., in Celsius).
     * @param hum Current ambient humidity reading (float, e.g., in %).
     * @param light Current ambient light intensity reading (float, e.g., in Lux or a raw ADC value).
     * @param tempMin Configured minimum temperature threshold for control logic (float).
     * @param tempMax Configured maximum temperature threshold for control logic (float).
     * @param humMin Configured minimum humidity threshold for control logic (float).
     * @param humMax Configured maximum humidity threshold for control logic (float).
     * @param lightMin Configured minimum light intensity threshold for control logic (float).
     * @param lightMax Configured maximum light intensity threshold for control logic (float).
     * @param r1 Boolean state of Relay 1 at the time of logging (`true` for ON, `false` for OFF).
     * @param r2 Boolean state of Relay 2.
     * @param r3 Boolean state of Relay 3.
     * @param r4 Boolean state of Relay 4.
     */
    void logData(const char* dateTime,
                 float temp, float hum, float light,
                 float tempMin, float tempMax,
                 float humMin, float humMax,
                 float lightMin, float lightMax,
                 bool r1, bool r2, bool r3, bool r4);

    /**
     * @brief Logs an event message with a timestamp to a separate event log file on the SD card.
     *
     * The target filename is specified by `EVENT_LOG_FILENAME` in `config.h` (e.g., "/event_log.txt").
     * Each event is logged on a new line in the file, formatted as:
     * `"DateTime - EventMessage"`
     * For example: `"2023-10-27 10:30:00 - System successfully restarted after update."`
     *
     * Error Handling:
     * Similar to `logData()`, if opening or writing to the event log file fails, this method
     * will automatically attempt to `reInit()` the SD card once. If successful, it retries
     * the logging. If still unsuccessful, an error message is handled.
     *
     * @param dateTime A C-style string representing the current date and time of the event
     *                 (e.g., "YYYY-MM-DD HH:MM:SS").
     * @param eventMessage The descriptive message string for the event to be logged (e.g., "WiFi Connected", "Low Battery Warning").
     */
    void logEvent(const char* dateTime, const char* eventMessage);

private:
    /**
     * @brief Pointer to the `LCDDisplay` object provided during construction.
     * This is used for displaying status messages (e.g., "SD Card Initialized", "Error Logging Data")
     * or errors related to SD card operations directly to the user via an LCD screen.
     * It can be `nullptr` if no LCD is used with this logger.
     */
    LCDDisplay* _lcd;
    /**
     * @brief Internal flag indicating the current operational status of the SD card.
     * `true` if the SD card has been successfully initialized (via `begin()` or `reInit()`)
     * and is currently considered operational for read/write operations.
     * `false` if initialization failed, the card is not present, or an unrecoverable error
     * has occurred (e.g., multiple failed `reInit()` attempts).
     * This flag is checked before attempting logging operations and updated by `begin()` and `reInit()`.
     */
    bool _sdCardOk;
};

#endif // SDCARD_LOGGER_H