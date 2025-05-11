/**
 * @file GPRSManager.h
 * @brief Defines the `GPRSManager` class for handling GPRS connectivity and asynchronous HTTP communication using a TinyGSM-compatible modem.
 *
 * This file declares the `GPRSManager` class, which concretely implements the `NetworkInterface`
 * abstract class to provide network services over a GPRS cellular connection. It is specifically
 * designed to work with GSM/GPRS modems supported by the TinyGSM library (e.g., SIM800 series,
 * SIM900, A6, A7, M590, etc., specified by `TINY_GSM_MODEM_*` in `config.h`).
 *
 * Core Responsibilities:
 * - **GPRS Connection Management**: Encapsulates the entire lifecycle of a GPRS connection,
 *   managed by an internal Finite State Machine (FSM) (`updateFSM()`). This includes:
 *     - Modem initialization (AT commands, serial checks, resets).
 *     - SIM card handling (PIN unlocking if `_simPin` is provided).
 *     - APN (Access Point Name) configuration (`_apn`, `_gprsUser`, `_gprsPass`).
 *     - Network registration and GPRS context activation.
 *     - Monitoring connection status and handling disconnections/reconnections.
 *     - Reporting GPRS status to a shared `DeviceState` object.
 * - **Asynchronous HTTP Operations**: Provides a mechanism to perform HTTP GET and POST
 *   requests over the established GPRS connection. These requests are also managed by an
 *   internal FSM (`updateHttpOperations()`), allowing for non-blocking behavior. This includes:
 *     - Connecting to the remote HTTP server.
 *     - Sending HTTP headers (including an optional "Authorization" token via `_authToken`).
 *     - Sending request payloads (for POST requests).
 *     - Receiving and buffering HTTP responses.
 *     - Parsing JSON responses using `ArduinoJson`.
 *     - Invoking user-provided callback functions (`_asyncCb`) with the parsed JSON.
 *     - Handling HTTP-level errors and retries (up to `MAX_HTTP_RETRIES` from `config.h`).
 * - **Status Reporting**: Optionally interacts with an `LCDDisplay` for visual feedback
 *   on GPRS and HTTP operations. Provides methods like `getStatusString()`, `getSignalQuality()`,
 *   and `getIPAddress()`.
 *
 * Key Dependencies:
 * - `config.h`: Provides compile-time configurations such as the specific modem type
 *   (`TINY_GSM_MODEM_*`), serial pins for the modem, buffer sizes (e.g., `GPRS_RESPONSE_BUFFER_SIZE`,
 *   `GPRS_MAX_HOST_LEN`), timeout values (e.g., `GPRS_STATE_TIMEOUT_MS`), retry counts
 *   (e.g., `MAX_MODEM_RESETS`, `MAX_HTTP_RETRIES`), and the size for the JSON document
 *   (`JSON_DOC_SIZE_DEVICE_CONFIG`).
 * - `TinyGsmClient.h` (TinyGSM library): Provides the underlying modem control and TCP client functionality.
 * - `ArduinoJson.h`: Used for parsing JSON responses from HTTP requests.
 * - `DeviceState.h`: Defines the `GPRSState` enum and is used to share the GPRS connection
 *   status globally.
 * - `NetworkInterface.h`: The base class defining the common network operations interface.
 *
 * @note The manager assumes a single active asynchronous HTTP operation at a time.
 *       HTTPS support (via `TinyGsmClientSecure`) is commented out as it often requires
 *       specific modem capabilities and more resources than available on typical microcontrollers
 *       with basic GPRS modems like SIM800L.
 */
#ifndef GPRS_MANAGER_H
#define GPRS_MANAGER_H

#include "config.h"           // Essential: TINY_GSM_MODEM_*, serial pins, buffer sizes, timeouts, retry limits, JSON_DOC_SIZE_DEVICE_CONFIG.
#include "NetworkInterface.h" // Defines the base class NetworkInterface and its virtual methods.
#include "DeviceState.h"      // Provides `GPRSState` enum and `DeviceState` struct for global status.
#include <TinyGsmCommon.h>   // Core TinyGSM definitions.
#include <TinyGsmClient.h>   // `TinyGsmClient` for TCP/IP over GPRS (used for HTTP).
// #include <TinyGsmClientSecure.h> // For HTTPS - typically requires specific modem features and more resources.
#include <ArduinoJson.h>     // For parsing/creating JSON (HTTP response/request bodies).

// Forward declarations
class LCDDisplay; // Optional, for displaying status messages.
// struct DeviceState; // Already included via DeviceState.h.
// class TinyGsm;      // The actual TinyGsm modem object (e.g., TinyGsmSim800 from config.h) is passed by reference.

/**
 * @class GPRSManager
 * @brief Manages GPRS network connectivity and executes asynchronous HTTP requests using a TinyGSM-compatible modem.
 *
 * This class implements the `NetworkInterface` to provide a standardized way to handle
 * network operations over a GPRS cellular connection. It encapsulates the complexities of:
 * 1.  **GPRS Connection Lifecycle (GPRS FSM)**: Managed by `updateFSM()`. This FSM handles
 *     modem initialization, SIM PIN, APN setup, network registration (e.g., CREG), GPRS
 *     attachment (e.g., CGATT), connection monitoring, and automatic reconnection attempts.
 *     It updates the `_deviceState->gprsState` with the current status.
 * 2.  **Asynchronous HTTP Transactions (HTTP FSM)**: Managed by `updateHttpOperations()`.
 *     Once GPRS is operational, this FSM handles individual HTTP GET/POST requests. It
 *     manages connecting to the host, sending the request (headers, optional auth token, payload),
 *     receiving the response, parsing JSON, and invoking a callback. It supports retries
 *     for transient errors.
 *
 * The manager relies heavily on constants defined in `config.h` for modem type,
 * serial communication, timeouts, and buffer sizes.
 */
class GPRSManager : public NetworkInterface {
public:
    /**
     * @brief Constructs a `GPRSManager` instance.
     * Initializes member variables with provided parameters and sets up internal state.
     * The actual modem communication and GPRS connection sequence are initiated by calling `connect()`
     * and then regularly calling `updateFSM()` and `updateHttpOperations()`.
     *
     * @param modem Reference to an initialized `TinyGsm` modem object (e.g., an instance of `TinyGsmSim800`
     *              defined based on `TINY_GSM_MODEM_SIM800` from `config.h`). This object is used for all
     *              AT command interactions with the modem.
     * @param apn The Access Point Name (APN) string for the GPRS network provider. Max length defined by `GPRS_APN_MAX_LEN`.
     * @param gprsUser Username for GPRS connection, if required by the APN provider. Can be empty or `nullptr`. Max length: `GPRS_USER_MAX_LEN`.
     * @param gprsPass Password for GPRS connection, if required by the APN provider. Can be empty or `nullptr`. Max length: `GPRS_PWD_MAX_LEN`.
     * @param simPin PIN for the SIM card, if the SIM is PIN-locked. Can be empty or `nullptr` if no PIN is set or
     *               if the SIM is already unlocked. Max length: `SIM_PIN_MAX_LEN`.
     * @param authToken The default authentication token (e.g., "Bearer <your_token>") to be used for API requests
     *                  if the `needsAuth` parameter in `startAsyncHttpRequest()` is true. Max length: `API_TOKEN_MAX_LEN`.
     * @param deviceState Pointer to the global `DeviceState` object. This manager will update `deviceState->gprsState`
     *                    to reflect the current status of the GPRS connection.
     * @param lcd Optional pointer to an `LCDDisplay` object. If provided, status messages and errors related to
     *            GPRS and HTTP operations may be displayed on the LCD. Defaults to `nullptr`.
     */
    GPRSManager(
        TinyGsm& modem,
        const char* apn,
        const char* gprsUser,
        const char* gprsPass,
        const char* simPin,
        const char* authToken,
        DeviceState* deviceState,
        LCDDisplay* lcd = nullptr
    );

    /**
     * @brief Destructor for `GPRSManager`.
     * Ensures proper cleanup, primarily by attempting to disconnect from the GPRS network
     * if a connection is active (`_gprsClient.stop()`, `_modem.gprsDisconnect()`).
     */
    ~GPRSManager() override;

    /**
     * @brief Attempts to initiate a connection to the GPRS network by starting the GPRS FSM.
     *
     * This method transitions the internal GPRS FSM to `GPRS_INIT_START`. The actual connection
     * process (modem power-up checks, SIM PIN, APN setup, network registration, GPRS attach)
     * occurs asynchronously as `updateFSM()` is called repeatedly in the main loop.
     *
     * @return `true` if the connection sequence was successfully initiated (i.e., GPRS FSM transitioned
     *         from an idle or disconnected state to an initial connection state).
     * @return `false` if the GPRS FSM is already in a connecting, connected, or error state that
     *         prevents initiating a new connection attempt at this moment.
     *
     * @note This method is non-blocking. To monitor the actual connection progress and status,
     *       check `isConnected()`, observe `_deviceState->gprsState`, or parse `getStatusString()`
     *       after repeatedly calling `updateFSM()`.
     */
    bool connect() override;

    /**
     * @brief Disconnects from the GPRS network.
     *
     * This method attempts to gracefully terminate the GPRS connection.
     * It typically involves:
     * - Stopping any active `_gprsClient` TCP connections.
     * - Instructing the modem to detach from the GPRS service (`_modem.gprsDisconnect()`).
     * - Transitioning the GPRS FSM to `GPRS_DISCONNECTED` or `GPRS_IDLE`.
     * The actual disconnection might take some time and is handled by the GPRS FSM via `updateFSM()`.
     */
    void disconnect() override;

    /**
     * @brief Checks if the GPRS connection is currently established and operational for data transfer.
     *
     * This primarily checks if the GPRS FSM is in the `GPRS_OPERATIONAL` state.
     * It implies that the modem is registered, GPRS context is active, and an IP address is likely assigned.
     *
     * @return `true` if the GPRS connection is active and ready for HTTP requests.
     * @return `false` otherwise (e.g., disconnected, connecting, error state).
     */
    bool isConnected() const override;

    /**
     * @brief Initiates an asynchronous HTTP request (GET or POST) over the GPRS connection.
     *
     * This method queues an HTTP request to be processed by the internal HTTP FSM
     * (driven by `updateHttpOperations()`). It does not block.
     * The GPRS connection must be operational (`isConnected()` should be true).
     * Only one asynchronous HTTP operation can be active at a time.
     *
     * @param url The target URL for the HTTP request (e.g., "http://api.example.com/data").
     *            Host, port, and path will be parsed from this URL. Max length: `API_URL_MAX_LEN`.
     * @param method The HTTP method string (e.g., "GET", "POST").
     * @param apiType A user-defined descriptive string for this API call type (e.g., "THL_DATA_POST", "CONFIG_GET").
     *                Useful for logging and debugging.
     * @param payload The request body string, typically for "POST" requests. Can be `nullptr` or empty for "GET".
     *                If provided for POST, "Content-Type: application/json" and "Content-Length" headers
     *                are typically added automatically.
     * @param cb A `std::function<bool(JsonDocument& doc)>` callback. This function will be invoked
     *           upon successful completion of the HTTP request and parsing of its JSON response body.
     *           The `JsonDocument` passed to the callback contains the parsed response.
     *           The callback should return `true` if it successfully processed the data, `false` otherwise.
     *           A `false` return may influence retry logic or error reporting.
     * @param needsAuth If `true` (default), the current `_authToken` will be included in the request
     *                  as an "Authorization: Bearer <token>" header. If `false`, no authorization header is added.
     *
     * @return `true` if the HTTP request was successfully initiated (i.e., added to the HTTP FSM queue).
     * @return `false` if another HTTP operation is already in progress (`_asyncOperationActive` is true),
     *         if GPRS is not connected (`!isConnected()`), or if essential parameters like URL or callback are invalid.
     */
    bool startAsyncHttpRequest(
        const char* url,
        const char* method,
        const char* apiType,
        const char* payload,
        std::function<bool(JsonDocument& doc)> cb,
        bool needsAuth = true
    ) override;

    /**
     * @brief Processes ongoing asynchronous HTTP operations via the HTTP FSM.
     *
     * This method must be called repeatedly from the main application loop. It drives the
     * state transitions of the active HTTP request, including:
     * - Connecting the `_gprsClient` to the remote server.
     * - Sending HTTP request headers and body.
     * - Receiving HTTP response headers and body.
     * - Handling timeouts and retries (up to `MAX_HTTP_RETRIES`).
     * - Parsing JSON response and invoking the `_asyncCb` callback.
     * It only performs actions if `_asyncOperationActive` is true.
     */
    void updateHttpOperations() override;

    /**
     * @brief Provides a human-readable status string describing the current GPRS connection state.
     *
     * This can include the current GPRS FSM state name (e.g., "GPRS_OPERATIONAL"),
     * signal quality (CSQ), network registration status, and the assigned IP address if connected.
     * Useful for debugging and display purposes.
     *
     * @return `String` object containing the current GPRS status summary.
     */
    String getStatusString() const override;

    /**
     * @brief Sets a new authentication token to be used for subsequent API requests that require authentication.
     * This updates the internal `_authToken` member.
     * @param authToken The new authentication token string (e.g., "Bearer <new_token>").
     *                  The content is copied into an internal buffer of size `API_TOKEN_MAX_LEN`.
     */
    void setAuthToken(const char* authToken);

    /**
     * @brief Gets the current GPRS signal quality (CSQ) value from the modem.
     *
     * The CSQ value reported by `_modem.getSignalQuality()` typically ranges from:
     * - 0 to 31: Indicating signal strength (higher is better).
     * - 99: Not known or not detectable.
     * This function returns the raw CSQ value.
     *
     * @return `int` representing the raw signal quality (CSQ). Returns a value like 99 or a negative
     *         number if the modem is not ready or an error occurs while querying.
     */
    int getSignalQuality() const;

    /**
     * @brief Checks the GPRS modem's network registration and GPRS attachment status more thoroughly.
     *
     * This queries the modem directly for:
     * - Network registration status (e.g., using `AT+CREG?` which returns if registered, roaming, etc.).
     * - GPRS attachment status (e.g., using `AT+CGATT?` which returns if GPRS service is available and attached).
     * This is a more direct check than `isConnected()`, which relies on the FSM state.
     *
     * @return `true` if the modem reports being registered on the network AND attached to GPRS service.
     * @return `false` otherwise, or if the modem is not responsive.
     */
    bool isModemConnected() const;

    /**
     * @brief Gets the current IP address assigned to the ESP32 by the GPRS network.
     * This is queried directly from the modem using `_modem.getLocalIP()`.
     *
     * @return `String` containing the IP address (e.g., "10.0.1.100").
     * @return An empty string or "0.0.0.0" if not connected or if an IP address is not yet available.
     */
    String getIPAddress() const;

    /**
     * @brief Updates the GPRS connection Finite State Machine (FSM).
     *
     * This method is the heart of the GPRS connection management. It must be called
     * regularly (e.g., in the main `loop()`) to:
     * - Progress through modem initialization steps (serial check, reset, SIM PIN, APN).
     * - Attempt network registration and GPRS attachment.
     * - Monitor the health of an active connection.
     * - Handle timeouts within states (using `GPRS_STATE_TIMEOUT_MS`).
     * - Manage reconnection attempts (`_gprsReconnectAttempt`, `MAX_GPRS_RECONNECTS`).
     * - Handle persistent errors, potentially triggering modem resets (`_modemResetCount`, `MAX_MODEM_RESETS`).
     * It directly calls the appropriate `handleGprs...()` private method based on `_currentGprsState`
     * and updates `_deviceState->gprsState` via `transitionToState()`.
     */
    void updateFSM();

private:
    // --- GPRS Connection Finite State Machine (FSM) ---
    // These private methods implement the logic for each state of the GPRS connection FSM.
    // They are called exclusively by `updateFSM()`.

    /**
     * @brief Transitions the GPRS FSM to a new state.
     * Updates `_currentGprsState`, `_deviceState->gprsState`, resets `_lastGprsStateTransitionTime`,
     * and logs the transition if `DEBUG_MODE_GPRS` is enabled.
     * @param newState The `GPRSState` (from `DeviceState.h`) to transition to.
     */
    void transitionToState(GPRSState newState);

    /** @brief GPRS FSM state handler for `GPRS_INIT_START`: Initiates the modem power-on and basic communication checks. May involve soft/hard reset planning. Transitions to `GPRS_INIT_WAIT_SERIAL` or `GPRS_INIT_RESET_MODEM`. */
    void handleGprsInitStart();
    /** @brief GPRS FSM state handler for `GPRS_INIT_WAIT_SERIAL`: Waits for the modem's serial interface to become responsive (e.g., responds to "AT"). Uses `MODEM_SERIAL_CHECK_INTERVAL_MS` and `MODEM_SERIAL_CHECK_RETRIES`. Transitions to `GPRS_INIT_RESET_MODEM` on success, or an error state on failure. */
    void handleGprsInitWaitSerial();
    /** @brief GPRS FSM state handler for `GPRS_INIT_RESET_MODEM`: Attempts to reset the modem (soft reset first, then hard reset if configured and supported via `MODEM_RST_PIN`). Manages `_modemResetCount`. Transitions to `GPRS_INIT_SET_APN` on successful reset sequence, or an error state. */
    void handleGprsInitResetModem();
    /** @brief GPRS FSM state handler for `GPRS_INIT_SET_APN`: Configures the APN, GPRS username, and password on the modem. Also handles SIM PIN unlocking if `_simPin` is set. Retries APN setting `MAX_APN_SET_RETRIES` times. Transitions to `GPRS_INIT_ATTACH_GPRS` on success. */
    void handleGprsInitSetApn();
    /** @brief GPRS FSM state handler for `GPRS_INIT_ATTACH_GPRS`: Attempts to register on the network and attach to the GPRS service. Monitors `_modem.isNetworkConnected()` and `_modem.isGprsConnected()`. Retries `MAX_GPRS_ATTACH_FAILURES` times. Transitions to `GPRS_OPERATIONAL` (or `GPRS_INIT_CONNECT_TCP` if used) on success. */
    void handleGprsInitAttachGprs();
    /** @brief GPRS FSM state handler for `GPRS_INIT_CONNECT_TCP`: (Optional/Placeholder) If GPRS attach alone isn't sufficient, this state can verify basic TCP connectivity (e.g., to a test server). Often, `GPRS_OPERATIONAL` is entered directly after successful GPRS attach. Manages `_tcpConnectFailCount`. */
    void handleGprsInitConnectTcp();
    /** @brief GPRS FSM state handler for `GPRS_OPERATIONAL`: The GPRS connection is active. This state periodically checks `_modem.isGprsConnected()`. If connection drops, transitions to `GPRS_CONNECTION_LOST`. */
    void handleGprsOperational();
    /** @brief GPRS FSM state handler for `GPRS_CONNECTION_LOST`: Entered when an active GPRS connection is unexpectedly lost. Initiates the reconnection process by transitioning to `GPRS_RECONNECTING`. */
    void handleGprsConnectionLost();
    /** @brief GPRS FSM state handler for `GPRS_RECONNECTING`: Manages the process of trying to re-establish a lost GPRS connection. Increments `_gprsReconnectAttempt`. Transitions to `GPRS_INIT_ATTACH_GPRS` to retry. If `MAX_GPRS_RECONNECTS` is exceeded, may transition to `GPRS_ERROR_RESTART_MODEM`. */
    void handleGprsReconnecting();
    /** @brief GPRS FSM state handler for `GPRS_ERROR_RESTART_MODEM`: Entered after repeated GPRS connection failures. Attempts a more forceful modem restart (hard reset if possible), increments `_modemResetCount`. If `MAX_MODEM_RESETS` exceeded, transitions to `GPRS_ERROR_MODEM_FAIL`. Otherwise, retries from `GPRS_INIT_START`. */
    void handleGprsErrorRestartModem();
    /** @brief GPRS FSM state handler for `GPRS_ERROR_MODEM_FAIL`: Terminal error state. Entered if the modem becomes persistently unresponsive or fails to connect after multiple resets and retry cycles. The system may require manual intervention or a power cycle. */
    void handleGprsErrorModemFail();

    /**
     * @brief Performs a hard reset of the modem if `MODEM_RST_PIN` is defined in `config.h`.
     * Toggles the reset pin according to `MODEM_RST_PULSE_MS`.
     * @return `true` if a hard reset was attempted (pin defined and toggled).
     * @return `false` if `MODEM_RST_PIN` is not defined (hard reset not supported by hardware configuration).
     */
    bool performModemHardReset();
    /**
     * @brief Performs a soft reset of the modem using AT commands (e.g., `AT+CFUN=1,1` or `_modem.restart()`).
     * @return `true` if the reset command was sent successfully and the modem acknowledged or began reset sequence.
     * @return `false` if the command failed or the modem did not respond as expected.
     */
    bool performModemSoftReset();
    /**
     * @brief Checks if the modem is responding to basic AT commands (typically "AT").
     * @return `true` if the modem sends an "OK" (or similar positive) response within a short timeout.
     * @return `false` otherwise (no response, error response, or timeout).
     */
    bool checkModemSerial();
    /**
     * @brief Calculates the time elapsed since the GPRS FSM last transitioned to its current state.
     * Used for implementing timeouts within specific GPRS FSM states (e.g., timeout for APN setting).
     * Compares `millis()` with `_lastGprsStateTransitionTime`.
     * @return Elapsed time in milliseconds since the last GPRS FSM state change.
     */
    unsigned long getElapsedTimeInCurrentGprsState() const;
    /**
     * @brief Converts a `GPRSState` enum value to its corresponding string representation.
     * Useful for logging and debugging messages.
     * @param state The `GPRSState` enum value (from `DeviceState.h`).
     * @return A constant C-string representing the state name (e.g., "GPRS_OPERATIONAL", "GPRS_INIT_START"). Returns "GPRS_UNKNOWN" for invalid state values.
     */
    static const char* gprsStateToString(GPRSState state);

    // --- GPRS FSM State Variables ---
    GPRSState _currentGprsState;                ///< Tracks the current state of the GPRS connection FSM. This value is also reflected in `_deviceState->gprsState`.
    unsigned long _lastGprsStateTransitionTime; ///< Timestamp (`millis()`) of when the GPRS FSM last changed state. Used for state-specific timeouts (e.g., `GPRS_STATE_TIMEOUT_MS`).
    uint8_t _gprsReconnectAttempt;              ///< Counter for consecutive GPRS reconnection attempts made in the `GPRS_RECONNECTING` state. Reset when connection is successful or a major error occurs. Compared against `MAX_GPRS_RECONNECTS`.
    uint8_t _modemResetCount;                   ///< Counter for how many times the modem has been reset (soft or hard) during the current overall connection attempt lifecycle. Compared against `MAX_MODEM_RESETS`.
    uint8_t _gprsAttachFailCount;               ///< Counter for consecutive failures specifically during the `GPRS_INIT_ATTACH_GPRS` state (network registration/GPRS attach). Compared against `MAX_GPRS_ATTACH_FAILURES`.
    uint8_t _tcpConnectFailCount;               ///< Counter for consecutive failures in the `GPRS_INIT_CONNECT_TCP` state (if this step is actively used for TCP tests). Compared against `MAX_TCP_CONNECT_FAILURES`.
    uint8_t _apnSetRetryCount;                  ///< Counter for retries when setting the APN (and SIM PIN) in the `GPRS_INIT_SET_APN` state. Compared against `MAX_APN_SET_RETRIES`.

    // --- Asynchronous HTTP Request Finite State Machine (FSM) ---
    // These members support the FSM that manages a single asynchronous HTTP request at a time.
    // This FSM is driven by `updateHttpOperations()`.

    /**
     * @enum GPRSHttpState
     * @brief Defines the states for the Finite State Machine (FSM) that manages asynchronous HTTP requests over GPRS.
     * This FSM handles the lifecycle of a single HTTP transaction, from initiation to completion or error.
     */
    enum class GPRSHttpState {
        IDLE,                   ///< HTTP FSM is idle; no active request. Ready to start a new one via `startAsyncHttpRequest()`.
        CLIENT_CONNECT,         ///< `_gprsClient` is attempting to connect to the remote HTTP server (host `_gprsHost`, port `_gprsPort`). Uses `HTTP_CONNECT_TIMEOUT_MS`.
        SENDING_REQUEST,        ///< Actively sending the HTTP request (method, path, headers, and body if `_asyncPayload` exists) to the connected server. Uses `HTTP_SEND_TIMEOUT_MS`.
        HEADERS_RECEIVING,      ///< Waiting for and receiving HTTP response headers from the server. Looks for status line and important headers like Content-Length. Uses `HTTP_HEADER_TIMEOUT_MS`.
        BODY_RECEIVING,         ///< Receiving the HTTP response body. Handles `_gprsContentLength` or chunked encoding. Accumulates data in `_gprsResponseBuffer`. Uses `HTTP_BODY_TIMEOUT_MS`.
        PROCESSING_RESPONSE,    ///< All response data received (or timeout). Now parsing `_gprsResponseBuffer` (typically as JSON into `_jsonDoc`) and invoking the user callback `_asyncCb`.
        COMPLETE,               ///< HTTP request lifecycle finished successfully (response processed, callback returned `true`). Transitions back to `IDLE`.
        RETRY_WAIT,             ///< A retryable error occurred (e.g., timeout, server error 5xx). Waiting for `HTTP_RETRY_DELAY_MS` before transitioning back to `CLIENT_CONNECT` to retry the request (if `_httpRetries < MAX_HTTP_RETRIES`).
        ERROR                   ///< An unrecoverable error occurred (e.g., non-retryable HTTP code, max retries exceeded, callback returned `false`). Transitions back to `IDLE`.
    };

    // --- Core GPRS and HTTP Components (Private Members) ---
    TinyGsm& _modem;           ///< Reference to the externally created and managed `TinyGsm` modem object (e.g., `TinyGsmSim800`). Used for all AT command communication.
    TinyGsmClient _gprsClient; ///< `TinyGsmClient` instance associated with `_modem`. Used for GPRS-based TCP/IP communication for HTTP requests.
                               ///< Note: For HTTPS, this would typically be `TinyGsmClientSecure` and would require the modem to support SSL/TLS and have necessary certificates/firmware.

    // --- Configuration and State Variables (Private Members) ---
    String _apn;        ///< Stores the Access Point Name (APN) for the GPRS network, copied from constructor. Max length `GPRS_APN_MAX_LEN`.
    String _gprsUser;   ///< Stores the username for GPRS connection (if required by APN), copied from constructor. Max length `GPRS_USER_MAX_LEN`.
    String _gprsPass;   ///< Stores the password for GPRS connection (if required by APN), copied from constructor. Max length `GPRS_PWD_MAX_LEN`.
    String _simPin;     ///< Stores the PIN for the SIM card (if it's PIN-locked), copied from constructor. Max length `SIM_PIN_MAX_LEN`.
    String _authToken;  ///< Stores the authentication token (e.g., "Bearer <token>") used for API requests if `_asyncNeedsAuth` is true. Updated by `setAuthToken()`. Max length `API_TOKEN_MAX_LEN`.
    DeviceState* _deviceState; ///< Pointer to the global `DeviceState` structure. Used to report `gprsState` to other parts of the system.
    LCDDisplay* _lcd;   ///< Optional pointer to an `LCDDisplay` object. If not `nullptr`, used for displaying status or debug messages.

    // --- Asynchronous HTTP Operation Variables (for the HTTP FSM) ---
    GPRSHttpState _currentHttpState; ///< Current state of the asynchronous GPRS HTTP request FSM. Determines logic in `updateHttpOperations()`.
    String _asyncUrl;                ///< Stores the full URL for the current or pending asynchronous HTTP request. Parsed into `_gprsHost`, `_gprsPath`, `_gprsPort`.
    String _asyncMethod;             ///< HTTP method (e.g., "GET", "POST") for the current asynchronous request.
    String _asyncApiType;            ///< User-defined descriptive string identifying the type of API call (e.g., "POST_SENSOR_DATA"). Useful for logging and debugging.
    String _asyncPayload;            ///< Stores the payload (body content, typically JSON) for the current asynchronous POST request. Empty for GET.
    std::function<bool(JsonDocument& doc)> _asyncCb; ///< The callback function to be invoked with the parsed JSON response upon successful completion of the async request.
    bool _asyncNeedsAuth;            ///< Flag indicating whether the current asynchronous request requires the `_authToken` to be sent in an "Authorization" header.
    unsigned long _asyncRequestStartTime; ///< Timestamp (`millis()`) marking when the current async HTTP request state (e.g., `CLIENT_CONNECT`, `SENDING_REQUEST`) began. Used for timeouts like `HTTP_CONNECT_TIMEOUT_MS`.
    bool _asyncOperationActive;      ///< Boolean flag that is `true` if an asynchronous HTTP operation is currently in progress (i.e., `_currentHttpState` is not `IDLE` or `COMPLETE`/`ERROR` just before reset to `IDLE`). Prevents starting new requests.
    uint8_t _httpRetries;            ///< Counter for the number of retries attempted for the current failing asynchronous HTTP request. Compared against `MAX_HTTP_RETRIES`.

    // --- GPRS-Specific HTTP Processing Variables (Used by HTTP FSM states) ---
    char _gprsHost[GPRS_MAX_HOST_LEN]; ///< Buffer to store the extracted hostname from `_asyncUrl` (e.g., "api.example.com"). Size from `config.h`.
    char _gprsPath[GPRS_MAX_PATH_LEN]; ///< Buffer to store the extracted path part from `_asyncUrl` (e.g., "/data/submit"). Size from `config.h`.
    int _gprsPort;                     ///< Extracted port number from `_asyncUrl`. Defaults to 80 for HTTP if not specified in the URL.
    String _gprsResponseBuffer;        ///< Buffer to accumulate the HTTP response body as it's received from `_gprsClient`. Cleared per request. Max size implicitly limited by `GPRS_RESPONSE_BUFFER_SIZE` or available memory.
    int _gprsHttpStatusCode;           ///< Stores the HTTP status code (e.g., 200, 404, 500) received from the server for the most recent GPRS HTTP request.
    unsigned long _gprsContentLength;  ///< Stores the `Content-Length` header value from the HTTP response, if provided by the server. Used in `BODY_RECEIVING` state.
    bool _gprsChunkedEncoding;         ///< Flag set to `true` if the HTTP response uses "Transfer-Encoding: chunked". Requires special handling in `BODY_RECEIVING`. (Note: Full chunked decoding might be complex for GPRSManager).
    unsigned long _gprsBodyBytesRead;  ///< Counter for the number of bytes read from the HTTP response body so far, used with `_gprsContentLength` or during chunked reading.

// Suppress deprecated declarations warning if `StaticJsonDocument` is from an older ArduinoJson version.
// Modern ArduinoJson (v6+) prefers `JsonDocument` as a base, but `StaticJsonDocument` is still valid for fixed-size allocation.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    /**
     * @brief A reusable `StaticJsonDocument` for parsing JSON responses from GPRS HTTP requests.
     * The size `JSON_DOC_SIZE_DEVICE_CONFIG` (defined in `config.h`) should be
     * adequately sized for the largest expected JSON response this manager will handle (e.g., device configuration).
     * Using `StaticJsonDocument` pre-allocates memory on the stack (if local) or globally/statically,
     * which can help reduce heap fragmentation risks compared to `DynamicJsonDocument` in long-running applications.
     * Cleared before each new JSON parsing attempt.
     */
    StaticJsonDocument<JSON_DOC_SIZE_DEVICE_CONFIG> _jsonDoc;
#pragma GCC diagnostic pop

    /**
     * @brief Determines if a given HTTP status code or TinyGSM client error code indicates a
     * potentially transient error for GPRS operations, suggesting a retry might succeed.
     *
     * Retryable conditions include:
     * - HTTP 408 (Request Timeout)
     * - HTTP 5xx (Server Errors)
     * - TinyGSM client specific error codes (negative values like -1, -2, -3) indicating connection issues.
     * Non-retryable include HTTP 2xx (Success), 4xx (Client Errors other than 408), etc.
     *
     * @param httpStatusCode The HTTP status code received from the server, or a negative TinyGSM client error code.
     * @return `true` if the error is considered retryable based on defined conditions (see `GPRS_RETRYABLE_HTTP_CODES` in `config.h` or internal logic).
     * @return `false` otherwise.
     */
    bool isRetryableError(int httpStatusCode);
 
    /**
     * @brief Prints detailed error information from the modem to the serial console.
     *
     * Useful for debugging GPRS connection or AT command issues. This might involve:
     * - Querying the modem for the last error code (e.g., `AT+CEER` for some modems, or modem-specific error commands).
     * - Printing any relevant error messages provided by the TinyGSM library.
     * This function is typically called when the GPRS FSM or HTTP FSM encounters a significant error.
     * Output is conditional on `DEBUG_MODE_GPRS` being enabled.
     */
    void printModemErrorCause();

    // --- Deprecated or Integrated Method Comments ---
    // The following methods were likely part of initial planning but their logic has been
    // integrated directly into the respective GPRS FSM state handler methods (`handleGprs...()`).
    // bool initializeModem();         // Logic now within GPRS_INIT_START, GPRS_INIT_WAIT_SERIAL, GPRS_INIT_RESET_MODEM states.
    // bool ensureNetworkRegistration(); // Logic now within GPRS_INIT_ATTACH_GPRS and monitored in GPRS_OPERATIONAL.
    // bool establishGPRSConnection();   // Logic primarily within GPRS_INIT_ATTACH_GPRS and GPRS_INIT_CONNECT_TCP states.
};

#endif // GPRS_MANAGER_H