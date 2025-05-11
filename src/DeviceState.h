/**
 * @file DeviceState.h
 * @brief Defines the overall device operational state and GPRS Finite State Machine (FSM) states.
 *
 * This file contains the `GPRSState` enum, which outlines the various stages of the GPRS
 * modem's connection lifecycle, and the `DeviceState` struct, which aggregates various
 * status flags, timing information, retry counters, and GPRS-specific states for the device.
 * This centralized state management helps in coordinating different modules and decision-making
 * processes within the application.
 */
#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <Arduino.h> // For `unsigned long` and other Arduino core types.
#include "config.h"  // For `INITIAL_RETRY_DELAY_MS`, `WIFI_RETRY_WHEN_GPRS_MS` and other config constants.

/**
 * @enum GPRSState
 * @brief Defines the states for the GPRS Finite State Machine (FSM).
 *
 * This enumeration represents the different operational and transitional states
 * of the GPRS modem and its connection process. The FSM transitions between these
 * states based on AT command responses, timeouts, and network events.
 */
enum class GPRSState {
    GPRS_STATE_INIT_START,          ///< Initial state: GPRS initialization sequence is about to begin.
    GPRS_STATE_INIT_WAIT_SERIAL,    ///< Waiting for the GPRS modem's serial interface to become available and responsive.
    GPRS_STATE_INIT_RESET_MODEM,    ///< Performing a hardware or software reset of the GPRS modem.
    // GPRS_STATE_INIT_SET_APN,     // (Removed) APN setting is now handled within the gprsConnect sequence of the GPRSManager.
    GPRS_STATE_INIT_ATTACH_GPRS,    ///< Attempting to attach to the GPRS network (CGATT command).
    // GPRS_STATE_INIT_CONNECT_TCP, // (Removed) TCP connection is part of general data transmission attempt, not a distinct init state.
    GPRS_STATE_OPERATIONAL,         ///< GPRS connection is active, IP address obtained, and ready for data transmission.
    GPRS_STATE_CONNECTION_LOST,     ///< GPRS connection or a data transmission attempt failed, or TCP connection was lost.
    GPRS_STATE_RECONNECTING,        ///< Actively trying to re-establish GPRS network attachment and/or TCP connection.
    GPRS_STATE_ERROR_RESTART_MODEM, ///< A significant error occurred (e.g., multiple attach failures), attempting to restart the modem.
    GPRS_STATE_ERROR_MODEM_FAIL,    ///< Modem is unresponsive or has failed critically (e.g., max resets reached). May require manual intervention or a longer recovery period.
    GPRS_STATE_DISABLED             ///< GPRS functionality is explicitly disabled (e.g., via `ENABLE_GPRS_FAILOVER` in `config.h` being false).
};

/**
 * @struct DeviceState
 * @brief Holds the collective state information for the device's operation.
 *
 * This structure aggregates various timers, counters, flags, and GPRS-specific
 * status information to provide a comprehensive overview of the device's current
 * operational state. It is used by multiple components to make decisions and
 * manage behavior.
 */
struct DeviceState {
    // --- Timing and Counters ---
    unsigned long lastLoopTime;                 ///< Timestamp (`millis()`) of the last main loop iteration.
    unsigned long lastApiAttemptTime;           ///< Timestamp of the last attempt to communicate with the API.
    unsigned long lastTimeSyncTime;             ///< Timestamp of the last successful RTC synchronization.
    unsigned long lastSuccessfulApiUpdateTime;  ///< Timestamp of the last successful data update to/from the API.
    unsigned long lastSdRetryTime;              ///< Timestamp of the last attempt to initialize or interact with the SD card after a failure.
    unsigned long lastConnectionRetryTime;      ///< Timestamp of the last general network connection retry attempt (WiFi or GPRS).
    unsigned long lastWiFiRetryWhenGprsTime;    ///< Timestamp of the last attempt to switch back to WiFi when operating on GPRS failover.
    unsigned long lastDeviceStatusCheckTime;    ///< Timestamp of the last check for device status/commands from the API (e.g., manual overrides).

    // --- Operational Flags ---
    bool isInFailSafeMode;                      ///< Flag indicating if the device is currently in a failsafe operational mode (e.g., due to prolonged network unavailability).
    // bool sdCardOk; // Removed: SD card status should be queried directly from SDCardLogger::isSdCardOk().

    // --- Network Retry Logic ---
    unsigned long currentConnectionRetryDelayMs;    ///< Current backoff delay for general network connection retries (increases exponentially).
    unsigned long currentWiFiSwitchBackoffDelayMs;  ///< Current backoff delay for attempts to switch from GPRS back to WiFi.

    // --- GPRS State Machine ---
    GPRSState currentGprsState;                 ///< The current state of the GPRS Finite State Machine.
    unsigned long lastGprsStateTransitionTime;  ///< Timestamp of the last transition in the GPRS FSM.
    uint8_t gprsModemResetCount;                ///< Counter for consecutive GPRS modem resets.
    uint8_t gprsAttachFailCount;                ///< Counter for consecutive GPRS network attach failures.
    // uint8_t gprsTcpConnectFailCount; // Removed, as GPRS_STATE_INIT_CONNECT_TCP state was removed. TCP failures contribute to GPRS_STATE_CONNECTION_LOST.

    // --- GPRS Specific Status ---
    bool isGprsConnected;                       ///< Flag indicating if GPRS reports an active connection (IP address obtained). This is a general GPRS status, not specific to a TCP socket.
    int16_t gprsSignalQuality;                  ///< GPRS signal quality (CSQ) value as reported by `AT+CSQ` (0-31 is valid, 99 indicates error/not known).

    // --- Web Manual Override States (Target states received from API) ---
    bool web_exhaust_target_state;              ///< Target state for the exhaust fan relay, as commanded by the web API.
    bool web_dehumidifier_target_state;         ///< Target state for the dehumidifier relay, as commanded by the web API.
    bool web_blower_target_state;               ///< Target state for the blower fan relay, as commanded by the web API.

    // --- Last Known Web Manual Override States (To detect changes from API commands) ---
    bool last_web_exhaust_target_state;         ///< Previous target state for the exhaust fan, used to detect changes.
    bool last_web_dehumidifier_target_state;    ///< Previous target state for the dehumidifier, used to detect changes.
    bool last_web_blower_target_state;          ///< Previous target state for the blower fan, used to detect changes.

    /**
     * @brief Constructor for DeviceState.
     * Initializes all members to default values.
     * - Timestamps are set to 0.
     * - Flags are generally set to false or their inactive state.
     * - Retry delays are initialized to their starting values from `config.h`.
     * - GPRS state is initialized to `GPRS_STATE_INIT_START`.
     * - GPRS signal quality is initialized to 99 (unknown/error).
     */
    DeviceState() :
        lastLoopTime(0),
        lastApiAttemptTime(0),
        lastTimeSyncTime(0),
        lastSuccessfulApiUpdateTime(0),
        lastSdRetryTime(0),
        lastConnectionRetryTime(0),
        lastWiFiRetryWhenGprsTime(0),
        lastDeviceStatusCheckTime(0),
        isInFailSafeMode(false),
        // sdCardOk(false), // Removed
        currentConnectionRetryDelayMs(INITIAL_RETRY_DELAY_MS),
        currentWiFiSwitchBackoffDelayMs(WIFI_RETRY_WHEN_GPRS_MS), // Initialize with the base interval from config.h
        web_exhaust_target_state(false),
        web_dehumidifier_target_state(false),
        web_blower_target_state(false),
        last_web_exhaust_target_state(false),
        last_web_dehumidifier_target_state(false),
        last_web_blower_target_state(false),
        currentGprsState(GPRSState::GPRS_STATE_INIT_START),
        lastGprsStateTransitionTime(0),
        gprsModemResetCount(0),
        gprsAttachFailCount(0),
        // gprsTcpConnectFailCount(0), // Removed
        isGprsConnected(false),
        gprsSignalQuality(99) // Initialize to unknown/error as per TinyGSM convention
         {}
};

#endif // DEVICE_STATE_H