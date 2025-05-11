/**
 * @file RTCManager.h
 * @brief Defines the `RTCManager` class for handling the DS3231 Real-Time Clock, including
 *        initialization, time synchronization, and time retrieval.
 *
 * This file declares the `RTCManager` class, which serves as the primary interface for all
 * operations related to the DS3231 Real-Time Clock (RTC) module. Its key responsibilities include:
 * - Initialization: Verifying the presence and operational status of the RTC hardware.
 *   It checks if the RTC has lost power (`_rtc.lostPower()`) which would indicate that its
 *   time is invalid.
 * - Time Synchronization: Maintaining accurate time by synchronizing the RTC with external
 *   network time sources. The manager supports multiple synchronization methods with a
 *   preference order:
 *   1.  NTP (Network Time Protocol): Used when a Wi-Fi connection is available.
 *       Configuration includes `NTP_SERVER` and `NTP_TIMEZONE_OFFSET_SECONDS` from `config.h`.
 *   2.  NITZ (Network Identity and Time Zone): Attempted when a GPRS connection is active,
 *       relying on time information provided by the cellular network.
 *   3.  HTTP World Time API: As a fallback, or if other methods fail, it can fetch the
 *       current time from a specified HTTP endpoint (e.g., `WORLDTIME_API_URL` from `config.h`
 *       or `DeviceConfig`). This is an asynchronous operation managed via `NetworkFacade`.
 * - Drift Management: Periodically checking the RTC's time against a reliable network source
 *   (primarily NTP) and re-synchronizing if the drift exceeds a configurable threshold
 *   (`RTC_DRIFT_THRESHOLD_SECONDS` from `config.h`).
 * - Time Provision: Providing the current date and time as a formatted string.
 *
 * The `RTCManager` interacts with:
 * - `NetworkFacade`: To determine network availability and type (WiFi/GPRS), and to initiate
 *   network requests for time synchronization (NTP lookups, HTTP GET requests for time API).
 * - `LCDDisplay`: To output status messages regarding RTC initialization, synchronization attempts,
 *   successes, and failures.
 * - `DeviceConfig`: Potentially to retrieve configurable settings like the `WORLDTIME_API_URL` if
 *   it's not hardcoded in `config.h`.
 *
 * Configuration constants such as `RTC_DRIFT_THRESHOLD_SECONDS`, `NTP_SERVER`,
 * `NTP_TIMEZONE_OFFSET_SECONDS`, `NTP_UPDATE_INTERVAL_MS`, and `WORLDTIME_API_URL` (or its
 * equivalent from `DeviceConfig`) are crucial for its operation and are typically defined
 * in `config.h`.
 */
#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <RTClib.h>         // Core library for DS3231 RTC (RTC_DS3231, DateTime classes).
#include <NTPClient.h>      // For interfacing with NTP servers (`NTPClient` class).
#include <WiFiUdp.h>        // UDP protocol support, required by `NTPClient`.
#include <ArduinoJson.h>    // For parsing JSON responses from HTTP-based time APIs.
#include "LCDDisplay.h"     // For displaying status messages to the user.
#include "config.h"         // For RTC_DRIFT_THRESHOLD_SECONDS, NTP_SERVER, NTP_TIMEZONE_OFFSET_SECONDS,
                            // NTP_UPDATE_INTERVAL_MS, WORLDTIME_API_URL, DEBUG_RTC etc.
#include "DeviceConfig.h"   // Potentially for dynamic configuration of WORLDTIME_API_URL.
#include "NetworkFacade.h"  // For initiating network requests (e.g., HTTP time sync) and checking network status.

// Forward declaration for WiFiManager is not strictly needed here as it's not directly used
// by RTCManager's public interface, but could be relevant if internal implementations change.

/**
 * @class RTCManager
 * @brief Manages all aspects of the Real-Time Clock (RTC) module, primarily the DS3231.
 *
 * This class encapsulates the logic for:
 * - Initializing the RTC and checking its health.
 * - Synchronizing the RTC's time with various network sources (NTP, NITZ, HTTP API).
 *   The choice of synchronization method often depends on the available network connection
 *   (Wi-Fi for NTP, GPRS for NITZ, or HTTP as a general fallback).
 * - Periodically checking for time drift against network time and correcting it.
 * - Providing the current date and time in a formatted manner.
 * It relies on `NetworkFacade` for network-dependent operations and `LCDDisplay` for user feedback.
 */
class RTCManager {
public:
    /**
     * @brief Constructs an `RTCManager` instance.
     * Initializes references to peripheral objects and configures the NTP client
     * with parameters like `NTP_SERVER` and `NTP_TIMEZONE_OFFSET_SECONDS` from `config.h`.
     *
     * @param d Reference to an `LCDDisplay` object, used for displaying status messages
     *          related to RTC operations (e.g., "RTC Init OK", "Syncing NTP...", "Time Set").
     * @param facade Reference to a `NetworkFacade` object. This is crucial for:
     *               - Checking network availability and type (WiFi/GPRS).
     *               - Initiating NTP synchronization requests.
     *               - Triggering asynchronous HTTP GET requests to a world time API.
     */
    RTCManager(LCDDisplay& d, NetworkFacade& facade);

    /**
     * @brief Initializes the RTC module (DS3231).
     *
     * This method performs the following steps:
     * 1.  Attempts to begin communication with the RTC module using `_rtc.begin()`.
     * 2.  Sets the `_rtcOk` flag based on the success of `_rtc.begin()`.
     * 3.  If communication is successful, it checks if the RTC has lost power since the last
     *     time it was set (using `_rtc.lostPower()`).
     * 4.  If power was lost or the RTC time is deemed invalid (e.g., year < 2023), it calls
     *     `initialTimeSync()` to attempt an immediate synchronization.
     * 5.  Displays status messages on the `_lcd_ref`.
     *
     * @return `true` if the RTC hardware was successfully initialized and is communicating.
     * @return `false` if the RTC module could not be found or initialized.
     */
    bool begin();

    /**
     * @brief Performs an initial time synchronization attempt if the RTC time is invalid.
     *
     * This method is typically called from `begin()` if the RTC's current time is suspect
     * (e.g., due to power loss or being a default uninitialized value like year 2000).
     * It tries to synchronize using the best available method:
     * - If Wi-Fi is connected (checked via `_networkFacade`), it attempts `syncNTP()`.
     * - If GPRS is available, it may attempt `syncNITZ()` or `triggerHttpTimeSync()`.
     * - If only HTTP is viable, it calls `triggerHttpTimeSync()`.
     * The specific logic depends on the `NetworkFacade`'s capabilities and current status.
     */
    void initialTimeSync();

    /**
     * @brief Adjusts the RTC's current time using a provided Unix epoch timestamp.
     * This is the core method used by all synchronization mechanisms (NTP, NITZ, HTTP callback)
     * once they have successfully obtained a valid epoch time.
     * It converts the epoch to a `DateTime` object and then calls `_rtc.adjust()`.
     *
     * @param epoch The Unix epoch time (number of seconds that have elapsed since
     *              January 1, 1970, at 00:00:00 Coordinated Universal Time (UTC)).
     */
    void adjustTime(uint32_t epoch);

    /**
     * @brief Periodically checks for RTC time drift against a reliable network time source
     *        (primarily NTP) and re-synchronizes if the drift exceeds a specified threshold.
     *
     * This method should be called regularly from the main application loop.
     * 1.  It fetches the current time from the RTC.
     * 2.  It attempts to get the current network time (e.g., via `_getNetworkDateTimeNTP()`).
     * 3.  If both times are valid, it calculates the difference (drift).
     * 4.  If the absolute drift is greater than `thresholdSeconds` (defaulting to
     *     `RTC_DRIFT_THRESHOLD_SECONDS` from `config.h`), it calls `adjustTime()`
     *     with the network time.
     *
     * @param thresholdSeconds The maximum allowed drift in seconds. If the RTC time differs
     *                         from the network time by more than this value, a synchronization
     *                         will be triggered. Defaults to `RTC_DRIFT_THRESHOLD_SECONDS`.
     * @return `true` if a synchronization was performed due to detected drift exceeding the threshold.
     * @return `false` if no synchronization was needed (drift was within limits, or network time
     *          could not be obtained).
     */
    bool checkAndSyncOnDrift(uint32_t thresholdSeconds = RTC_DRIFT_THRESHOLD_SECONDS);

    /**
     * @brief Attempts to synchronize the RTC with an NTP (Network Time Protocol) server.
     *
     * This method is typically used when a Wi-Fi connection is available and deemed reliable.
     * It uses the `_timeClient` (an `NTPClient` instance) to fetch the current UTC time.
     * If successful, it calls `adjustTime()` to update the RTC.
     * If NTP synchronization fails, it might fall back to `triggerHttpTimeSync()` as an alternative.
     *
     * @return `true` if NTP synchronization was successful and the RTC was updated.
     * @return `false` if NTP synchronization failed. (Note: A fallback to HTTP might still occur
     *          and succeed, but this function would have returned false for the NTP part).
     */
    bool syncNTP();

    /**
     * @brief Attempts to synchronize the RTC using NITZ (Network Identity and Time Zone)
     *        or falls back to an HTTP-based time service.
     *
     * NITZ is a feature of GSM/GPRS networks where the network provides time information.
     * This method would typically involve commands to the GPRS modem (via `NetworkFacade`)
     * to retrieve NITZ data. If NITZ is unavailable or fails, it may call `triggerHttpTimeSync()`
     * as a fallback. The actual implementation of NITZ retrieval depends heavily on the
     * GPRS modem's AT command set and the `NetworkFacade`'s GPRS manager.
     */
    void syncNITZ();

    /**
     * @brief Triggers an asynchronous HTTP GET request to a configured world time API
     *        (e.g., `WORLDTIME_API_URL` from `config.h` or `DeviceConfig`) to fetch the current epoch time.
     *
     * This method does **not** block or directly update the RTC. Instead, it initiates an
     * HTTP request through the `_networkFacade`. The `_networkFacade` is expected to handle
     * the asynchronous request and, upon successful completion and parsing of the JSON response
     * (which should contain the epoch time), invoke a callback. This callback (often a lambda
     * function passed to the `NetworkFacade`'s HTTP request method) will then call
     * `RTCManager::adjustTime()` with the received epoch.
     *
     * This is useful as a fallback when NTP/NITZ are unavailable or as the primary method
     * if desired.
     */
    void triggerHttpTimeSync();

    /**
     * @brief Gets the current date and time from the RTC as a formatted string.
     * The format is typically "YYYY-MM-DD HH:MM:SS".
     *
     * @return A `String` object containing the formatted date and time.
     *         If the RTC is not available or not initialized (`!_rtcOk`), it returns
     *         an error string like "RTC Error" or "RTC N/A".
     */
    String getFormattedDateTime();
    
    /**
     * @brief Checks if the RTC hardware was successfully initialized and is considered operational.
     * @return `true` if the RTC hardware was found and initialized correctly during `begin()`.
     * @return `false` otherwise (e.g., I2C communication failed).
     */
    bool isRtcOk() const;

private:
    bool _rtcOk; ///< Flag set to `true` if `_rtc.begin()` was successful, indicating RTC hardware is present and communicating.

    /**
     * @brief Internal helper function to fetch the current time from an NTP server.
     * This function is called by `syncNTP()` and `checkAndSyncOnDrift()`.
     * It uses `_timeClient.update()` to get the latest time and then `_timeClient.getEpochTime()`
     * to retrieve it as a Unix timestamp. This epoch is then used to construct a `DateTime` object.
     *
     * @param[out] dt Reference to a `DateTime` object where the fetched network time will be stored
     *                if the operation is successful.
     * @return `true` if time was successfully fetched from NTP and `dt` was updated.
     * @return `false` if fetching time from NTP failed (e.g., network unavailable, NTP server unreachable).
     */
    bool _getNetworkDateTimeNTP(DateTime& dt);

    RTC_DS3231 _rtc;             ///< Instance of the `RTC_DS3231` library object, providing the interface to the RTC chip.
    WiFiUDP _ntpUDP;           ///< `WiFiUDP` instance used by `NTPClient` for sending and receiving NTP packets over UDP.
    NTPClient _timeClient;       ///< `NTPClient` object configured with `NTP_SERVER`, `NTP_TIMEZONE_OFFSET_SECONDS`, and `NTP_UPDATE_INTERVAL_MS`.
    LCDDisplay& _lcd_ref;        ///< Reference to the `LCDDisplay` object for outputting status and informational messages.
    NetworkFacade& _networkFacade; ///< Reference to the `NetworkFacade` used for all network-dependent operations,
                                 ///< such as checking connectivity, initiating NTP lookups, or making HTTP requests for time.
};

#endif // RTC_MANAGER_H