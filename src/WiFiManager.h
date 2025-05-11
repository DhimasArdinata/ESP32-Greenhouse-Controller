/**
 * @file WiFiManager.h
 * @brief Defines the `WiFiManager` class for handling WiFi connectivity and asynchronous HTTP/HTTPS communication.
 *
 * This file declares the `WiFiManager` class, which concretely implements the `NetworkInterface`
 * abstract base class. Its primary responsibilities are:
 * - WiFi Connection Management: Establishing and maintaining a connection to a specified WiFi
 *   network using provided credentials (SSID and password). This includes handling connection
 *   retries (up to `WIFI_CONNECT_RETRIES` with `WIFI_CONNECT_TIMEOUT` per attempt, from `config.h`).
 * - Credential Management: Storing and allowing updates for WiFi credentials (`_ssid`, `_password`)
 *   and an authentication token (`_authToken`) used for API access.
 * - Asynchronous HTTP/HTTPS Operations: Performing HTTP GET and POST requests in a non-blocking
 *   manner. This is managed by an internal Finite State Machine (FSM) (`_currentHttpState`)
 *   driven by the `updateHttpOperations()` method, which must be called regularly.
 * - Error Handling and Retries: Managing transient network errors during HTTP requests with a
 *   retry mechanism (up to `MAX_HTTP_RETRIES` attempts, with `HTTP_RETRY_DELAY` between retries,
 *   and an overall `HTTP_TIMEOUT` per attempt, all from `config.h`).
 * - JSON Processing: Parsing JSON responses from HTTP requests using `ArduinoJson` into a
 *   pre-allocated `_jsonDoc` (size `JSON_DOC_SIZE_API_RESPONSE` or similar from `config.h`).
 * - Status Reporting: Optionally interacting with an `LCDDisplay` (if provided via constructor)
 *   for visual feedback on WiFi and HTTP operations.
 *
 * The class relies heavily on constants defined in `config.h` for timeouts, retry counts,
 * buffer sizes (e.g., JSON document size), and potentially the base URL for API endpoints.
 *
 * @note For HTTPS communication, the `_wifiClientInstance` would need to be a `WiFiClientSecure`
 *       object, and `_httpClient.begin()` would need to be called with this secure client,
 *       potentially along with a root CA certificate for server verification. The current
 *       implementation primarily shows HTTP but is structured to accommodate HTTPS with
 *       these modifications.
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"           // Crucial for WIFI_*, HTTP_*, JSON_DOC_SIZE_*, API_BASE_URL, DEBUG_MODE_WIFI etc.
#include "NetworkInterface.h" // Defines the abstract base class `NetworkInterface` and its contract.
#include <WiFi.h>             // ESP32 WiFi library for `WiFi`, `WiFiClient`.
#include <HTTPClient.h>       // ESP32 HTTP client library for `HTTPClient`.
#include <ArduinoJson.h>      // For `JsonDocument`, `StaticJsonDocument`, `deserializeJson()`.
#include <functional>         // For `std::function`, used for asynchronous HTTP request callbacks.

// Forward declaration for LCDDisplay to avoid circular dependencies.
class LCDDisplay;

/**
 * @class WiFiManager
 * @brief Manages WiFi network connectivity and executes asynchronous HTTP/HTTPS requests.
 *
 * This class provides a concrete implementation of the `NetworkInterface` tailored for WiFi-based
 * communication on ESP32 platforms. It encapsulates the detailed logic for:
 * 1.  **Connecting to a WiFi Access Point**: Using stored SSID and password, with retry logic.
 * 2.  **Disconnecting from WiFi**: Gracefully terminating the connection.
 * 3.  **Checking Connection Status**: Verifying if WiFi is currently active.
 * 4.  **Executing Asynchronous HTTP GET and POST Requests**: This is the core of its network
 *     operation capability, designed to be non-blocking. The `updateHttpOperations()` method
 *     must be called regularly from the application's main loop to drive the internal HTTP
 *     Finite State Machine (FSM). This FSM handles request initiation, sending, response
 *     processing, timeouts, and retries.
 * 5.  **Managing Authentication**: Stores an authentication token (`_authToken`) which can be
 *     automatically included in HTTP request headers.
 * 6.  **Interacting with LCD**: Optionally displays status messages (connection progress,
 *     HTTP outcomes) on an `LCDDisplay` object.
 */
class WiFiManager : public NetworkInterface {
public:
    /**
     * @brief Constructs a `WiFiManager` instance.
     * Initializes member variables with provided credentials and settings.
     * The actual WiFi connection is established by calling `connect()`.
     *
     * @param ssid The Service Set Identifier (SSID) of the WiFi network to connect to.
     *             This is copied into the internal `_ssid` string.
     * @param password The password for the WiFi network. This is copied into `_password`.
     * @param authToken The initial authentication token (e.g., a Bearer token) for API requests.
     *                  This is copied into `_authToken` and can be updated later via `setAuthToken()`.
     * @param lcd Optional pointer to an `LCDDisplay` object. If provided (not `nullptr`),
     *            status messages related to WiFi connection and HTTP operations
     *            (e.g., "WiFi Connecting...", "HTTP POST OK", "HTTP Error: 404") will be
     *            displayed on the LCD. Defaults to `nullptr`, in which case no LCD output occurs.
     */
    WiFiManager(const char* ssid, const char* password, const char* authToken, LCDDisplay* lcd = nullptr);
    
    /**
     * @brief Destructor for `WiFiManager`.
     * Ensures a graceful shutdown by:
     * - Disconnecting from WiFi if still connected (`WiFi.disconnect(true)`).
     * - Aborting any ongoing HTTP operation by calling `_httpClient.end()` to free resources.
     */
    ~WiFiManager() override;

    /**
     * @brief Attempts to connect to the configured WiFi network using the stored SSID and password.
     *
     * This method internally calls the private `connectWiFi()` helper, which handles the
     * actual connection logic including setting WiFi mode to STA, initiating `WiFi.begin()`,
     * waiting for connection with a timeout (`WIFI_CONNECT_TIMEOUT` from `config.h`),
     * and retrying connection up to `WIFI_CONNECT_RETRIES` times (from `config.h`).
     * Status messages are displayed on the LCD during this process.
     *
     * @return `true` if the WiFi connection is successfully established within the configured
     *         number of attempts and timeout period.
     * @return `false` if the connection fails after all retries or due to timeout.
     */
    bool connect() override;

    /**
     * @brief Disconnects from the currently connected WiFi network.
     *
     * This calls `WiFi.disconnect(true)` to terminate the WiFi connection.
     * It also ensures that the `_httpClient` is properly ended by calling `_httpClient.end()`
     * to release any resources if an HTTP operation was active or pending.
     * The HTTP FSM state is reset to `IDLE`.
     */
    void disconnect() override;

    /**
     * @brief Checks if the device is currently connected to a WiFi network.
     * This method primarily relies on the status reported by `WiFi.status() == WL_CONNECTED`.
     *
     * @return `true` if `WiFi.status()` indicates a connection (`WL_CONNECTED`).
     * @return `false` otherwise (e.g., `WL_DISCONNECTED`, `WL_IDLE_STATUS`, etc.).
     */
    bool isConnected() const override;

    /**
     * @brief Initiates an asynchronous HTTP request (GET or POST).
     *
     * This method sets up the parameters for an HTTP request and transitions the internal
     * HTTP Finite State Machine (`_currentHttpState`) to `BEGIN_REQUEST`. The actual network
     * communication (sending the request, receiving the response) occurs progressively as
     * the `updateHttpOperations()` method is called repeatedly from the main loop.
     *
     * **Important Constraints:**
     * - Only one asynchronous HTTP operation can be active at a time. Calling this method
     *   while `_asyncOperationActive` is `true` (i.e., another request is in progress)
     *   will result in an immediate `false` return.
     * - WiFi must be connected (`isConnected()` must be `true`).
     *
     * @param url The target URL for the HTTP request (e.g., "http://api.example.com/data").
     *            For HTTPS, ensure the URL starts with "https://".
     * @param method The HTTP method as a C-string (e.g., "GET", "POST"). This is case-sensitive.
     * @param apiType A user-defined descriptive string for this API call type (e.g., "DeviceConfigGet", "SensorDataPost").
     *                This is used primarily for logging and debugging, helping to identify the request.
     * @param payload The request body as a C-string, typically for "POST" or "PUT" requests.
     *                It can be `nullptr` or an empty string for "GET" requests or if no body is needed.
     *                If provided for POST/PUT, "Content-Type: application/json" (or as per `config.h`)
     *                and "Content-Length" headers are typically added automatically by `HTTPClient`.
     * @param cb A `std::function<bool(JsonDocument& doc)>` callback. This function will be invoked
     *           if the HTTP request completes successfully (typically with an HTTP 2xx status code) AND
     *           a JSON response is received and successfully parsed into the `_jsonDoc`.
     *           The `JsonDocument` passed to the callback contains the parsed response data.
     *           The callback should return `true` if it successfully processed the data from the document.
     *           Returning `false` from the callback usually indicates an application-level issue with
     *           the response content (e.g., expected fields missing), even if the HTTP transaction was okay.
     *           This `false` return will still mark the HTTP operation as `COMPLETE` but might be logged as an error.
     * @param needsAuth If `true` (the default), the current `_authToken` will be retrieved and included
     *                  in the request as an "Authorization: Bearer <token>" header. If `false`, no
     *                  authorization header is added by this mechanism.
     *
     * @return `true` if the request was successfully initiated. This means all parameters are valid,
     *         no other HTTP operation is currently active, WiFi is connected, and the request has been
     *         queued for processing by `updateHttpOperations()`.
     * @return `false` if initiation failed due to:
     *         - Another operation already in progress (`_asyncOperationActive` is `true`).
     *         - Essential parameters like `url` or `method` are missing/invalid.
     *         - WiFi is not currently connected.
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
     * @brief Drives the internal state machine for ongoing asynchronous HTTP operations.
     *
     * This method **must be called repeatedly and frequently** from the main application loop (`loop()`).
     * It is responsible for progressing the active HTTP request through its various lifecycle states,
     * as defined by `WiFiHttpState`. This includes:
     * - Initializing the `_httpClient` instance (`BEGIN_REQUEST`).
     * - Sending the actual HTTP request and headers (`SENDING_REQUEST`).
     * - Receiving and processing the server's response, including parsing JSON (`PROCESSING_RESPONSE`).
     * - Handling HTTP timeouts (based on `HTTP_TIMEOUT` from `config.h`).
     * - Managing retries for transient, retryable errors (up to `MAX_HTTP_RETRIES` with `HTTP_RETRY_DELAY`
     *   pauses, based on `config.h` values and `isRetryableError()` logic).
     * - Invoking the user-provided callback (`_asyncCb`) upon successful data retrieval.
     * - Transitioning to `COMPLETE` or `ERROR` states.
     *
     * Without regular calls to this function, asynchronous HTTP requests will stall and not complete.
     */
    void updateHttpOperations() override;

    /**
     * @brief Provides a human-readable status string describing the current WiFi connection state.
     * This can include the connection status (e.g., "Connected", "Connecting", "Disconnected"),
     * the SSID of the connected network, and the device's IP address if connected.
     *
     * @return A `String` object containing the current WiFi status summary. For example:
     *         "Connected to MyWiFi, IP: 192.168.1.101", "WiFi Connecting...", "WiFi Disconnected".
     */
    String getStatusString() const override;

    /**
     * @brief A more direct check of the WiFi connection status using `WiFi.status()`.
     *
     * This method can be useful to get the raw status directly from the ESP32 WiFi library,
     * without relying on any cached state within this `WiFiManager` class (although the
     * public `isConnected()` method also primarily uses `WiFi.status()`).
     *
     * @return `true` if `WiFi.status()` returns `WL_CONNECTED`.
     * @return `false` for any other `WiFi.status()` value.
     */
    bool isActuallyConnected() const;

    /**
     * @brief Gets the current IP address assigned to the ESP32 by the WiFi network.
     * Queries `WiFi.localIP()`.
     *
     * @return A `String` containing the IP address in dot-decimal notation (e.g., "192.168.1.100").
     * @return Returns "0.0.0.0" if the device is not connected to WiFi or if an IP address
     *         has not yet been assigned or is otherwise unavailable.
     */
    String getIPAddress() const;

    /**
     * @brief Updates the WiFi credentials (SSID and password) stored within the `WiFiManager`.
     *
     * The newly provided SSID and password will be copied into the internal `_ssid` and `_password`
     * members. These new credentials will then be used for any subsequent calls to `connect()`.
     * Note that calling this method does not automatically attempt to disconnect from the current
     * network or reconnect with the new credentials; `disconnect()` and/or `connect()` must be
     * called explicitly if an immediate change in connection is desired.
     *
     * @param ssid The new WiFi Service Set Identifier (SSID). Must not be `nullptr`.
     * @param password The new WiFi password. Must not be `nullptr`.
     */
    void setCredentials(const char* ssid, const char* password);

    /**
     * @brief Updates the authentication token used for API requests.
     *
     * The provided `authToken` is copied into the internal `_authToken` member. This token
     * will be included in the "Authorization: Bearer <token>" header of HTTP requests
     * if the `needsAuth` parameter was `true` when `startAsyncHttpRequest()` was called for
     * that specific request.
     *
     * @param authToken The new authentication token string. Must not be `nullptr`.
     */
    void setAuthToken(const char* authToken);

private:
    /**
     * @brief Internal helper function responsible for the actual process of establishing a WiFi connection.
     *
     * This function is called by the public `connect()` method. Its responsibilities include:
     * - Setting the WiFi mode to Station mode (`WIFI_STA`).
     * - Initiating the connection attempt using `WiFi.begin(_ssid.c_str(), _password.c_str())`.
     * - Waiting for the connection to be established, with a timeout specified by
     *   `WIFI_CONNECT_TIMEOUT` (from `config.h`). This wait loop checks `WiFi.status()`.
     * - Displaying status messages on the `_lcd` (if available) during the connection process
     *   (e.g., "WiFi Connecting...", "WiFi Connected to [SSID]", "WiFi Connect Failed").
     * - Retrying the connection attempt up to `WIFI_CONNECT_RETRIES` times (from `config.h`)
     *   if initial attempts fail. Each retry involves a short delay.
     *
     * @return `true` if the WiFi connection is successfully established (`WiFi.status() == WL_CONNECTED`).
     * @return `false` if all connection attempts (initial + retries) fail or if the timeout is reached
     *         during any attempt.
     */
    bool connectWiFi();

    /**
     * @enum WiFiHttpState
     * @brief Defines the states for the internal Finite State Machine (FSM) that manages the lifecycle of asynchronous HTTP requests.
     *
     * The `updateHttpOperations()` method transitions the FSM through these states:
     * - **`IDLE`**: The FSM is inactive, awaiting a new request. `_asyncOperationActive` is `false`. Transitions from `COMPLETE` or `ERROR`.
     * - **`BEGIN_REQUEST`**: Entered when `startAsyncHttpRequest()` is called successfully.
     *     - Action: Initializes `_httpClient.begin()` with the URL and `_wifiClientInstance`. Sets HTTP headers (User-Agent, Authorization if `_asyncNeedsAuth`). For POST/PUT, sets "Content-Type" and payload using `_httpClient.POST()` or similar. Sets `_asyncRequestStartTime`.
     *     - Transition: To `SENDING_REQUEST`.
     * - **`SENDING_REQUEST`**: The request has been prepared and is now being sent.
     *     - Action: For GET, calls `_httpClient.GET()`. For POST, this state might be brief if `POST()` was synchronous, or it waits if `sendRequest()` is used for chunked/streamed data. Populates `_httpStatusCode` with the server's response code.
     *     - Transition: To `PROCESSING_RESPONSE` if status code received. To `RETRY_WAIT` or `ERROR` if send fails or `HTTP_TIMEOUT` occurs (checked against `_asyncRequestStartTime`).
     * - **`PROCESSING_RESPONSE`**: The server has responded with an HTTP status code.
     *     - Action: Checks `_httpStatusCode`.
     *         - If success (2xx): Reads the response payload using `_httpClient.getString()`, deserializes it into `_jsonDoc` using `deserializeJson()`. If parsing succeeds, invokes `_asyncCb(_jsonDoc)`.
     *         - If error code: Calls `isRetryableError(_httpStatusCode)`.
     *     - Transition: To `COMPLETE` if successful processing or non-retryable error. To `RETRY_WAIT` if retryable error and `_httpRetries < MAX_HTTP_RETRIES`. To `ERROR` if max retries reached or other unrecoverable issue. `_httpClient.end()` is called before exiting this phase unless retrying.
     * - **`RETRY_WAIT`**: A retryable error occurred, and retries are pending.
     *     - Action: Waits for `HTTP_RETRY_DELAY` (from `config.h`). Increments `_httpRetries`.
     *     - Transition: To `BEGIN_REQUEST` to re-attempt the request.
     * - **`COMPLETE`**: The HTTP request lifecycle has finished. This can be due to successful completion
     *     (data processed by callback) or a non-retryable error after the request was sent (e.g., 404 Not Found, callback returned false).
     *     - Action: Logs completion status. Resets `_asyncOperationActive` to `false`. `_httpClient.end()` should have been called.
     *     - Transition: To `IDLE`.
     * - **`ERROR`**: An unrecoverable error occurred during the HTTP request process (e.g., connection failure before send, max retries exceeded, critical parsing failure).
     *     - Action: Logs the error. Resets `_asyncOperationActive` to `false`. `_httpClient.end()` should have been called.
     *     - Transition: To `IDLE`.
     */
    enum class WiFiHttpState {
        IDLE,                   ///< FSM is idle; no active HTTP request. Ready for `startAsyncHttpRequest`.
        BEGIN_REQUEST,          ///< Initializing a new HTTP request: `_httpClient.begin()`, setting headers, payload.
        SENDING_REQUEST,        ///< Actively sending the HTTP request (e.g., `_httpClient.GET()`, `_httpClient.POST()`) and awaiting server status code.
        PROCESSING_RESPONSE,    ///< Server status code received; now processing response body (reading, parsing JSON, invoking callback).
        RETRY_WAIT,             ///< A transient, retryable error occurred; FSM is pausing for `HTTP_RETRY_DELAY` before re-attempting.
        COMPLETE,               ///< HTTP request lifecycle finished (either success with callback, or a non-retryable error post-send).
        ERROR                   ///< An unrecoverable error occurred (e.g., pre-send failure, max retries exhausted, critical parse error).
    };

    String _ssid;       ///< Stores the Service Set Identifier (SSID) of the target WiFi network. Max length determined by String capacity.
    String _password;   ///< Stores the password for the target WiFi network. Max length determined by String capacity.
    String _authToken;  ///< Stores the authentication token (e.g., "Bearer YOUR_TOKEN_HERE") used for API requests requiring authorization. Max length from `config.h` if defined, or String capacity.
    LCDDisplay* _lcd;   ///< Optional pointer to an `LCDDisplay` object for showing status messages. If `nullptr`, no LCD output is attempted by this manager.

    HTTPClient _httpClient;         ///< ESP32 `HTTPClient` object used for making HTTP/HTTPS requests. One instance is reused for all requests.
                                    ///< For HTTPS, `_httpClient.begin()` must be called with a `WiFiClientSecure` instance and the server's root CA certificate (or `setInsecure()` for testing, not recommended for production).
    WiFiClient _wifiClientInstance; ///< `WiFiClient` instance used by `_httpClient` for standard HTTP communication.
                                    ///< For HTTPS, this member would typically be replaced or supplemented by a `WiFiClientSecure` object.
                                    ///< `WiFiClientSecure` needs to be configured (e.g., `setCACert()`, `setCertificate()`, `setPrivateKey()`)
                                    ///< depending on the server's SSL/TLS requirements and whether client authentication is needed.

    // --- Asynchronous HTTP Operation State Variables ---
    WiFiHttpState _currentHttpState; ///< Tracks the current state of the asynchronous HTTP request Finite State Machine (FSM).
    String _asyncUrl;                ///< Stores the URL for the active or pending asynchronous HTTP request.
    String _asyncMethod;             ///< Stores the HTTP method (e.g., "GET", "POST") for the active/pending async request.
    String _asyncApiType;            ///< User-defined descriptive string for the type of API call (e.g., "UploadSensorReadings", "FetchConfig"). Used for logging.
    String _asyncPayload;            ///< Stores the payload (body content, typically JSON) for the active/pending async POST or PUT request.
    std::function<bool(JsonDocument& doc)> _asyncCb; ///< The callback function to be invoked with the parsed JSON response upon successful completion of the async request.
    bool _asyncNeedsAuth;            ///< Flag indicating whether the active/pending asynchronous request requires the `_authToken` to be sent.
    unsigned long _asyncRequestStartTime; ///< Timestamp (`millis()`) marking when the current async HTTP request state (or its latest retry attempt) began. Used for implementing `HTTP_TIMEOUT`.
    bool _asyncOperationActive;      ///< Flag that is `true` if an asynchronous HTTP operation is currently in progress (i.e., `_currentHttpState` is not `IDLE`). Prevents starting new requests.
    int _httpStatusCode;             ///< Stores the HTTP status code received from the server for the most recent attempt of the current async request.
    uint8_t _httpRetries;            ///< Counter for the number of retries attempted for the current failing asynchronous HTTP request. Reset to 0 for each new request initiated by `startAsyncHttpRequest`. Incremented in `RETRY_WAIT` state. Max value `MAX_HTTP_RETRIES` from `config.h`.

    /**
     * @brief Determines if a given HTTP status code (or `HTTPClient` internal error code)
     * represents a transient error that might be resolved by retrying the request.
     *
     * Retryable conditions typically include:
     * - HTTP 408 (Request Timeout)
     * - HTTP 5xx (Server Errors like 500 Internal Server Error, 503 Service Unavailable)
     * - Negative error codes returned by `HTTPClient` (e.g., -1 for connection failed,
     *   -2 for send header failed, -4 for send payload failed, -5 for connection timed out).
     *
     * Non-retryable conditions usually include:
     * - HTTP 2xx (Success codes)
     * - Most HTTP 4xx (Client Errors like 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found), except specific ones like 408.
     * - HTTP status code 0 (often indicates no response received or client unable to connect).
     *
     * The decision to retry is also contingent on `_httpRetries < MAX_HTTP_RETRIES`.
     *
     * @param httpStatusCode The HTTP status code returned by `_httpClient.sendRequest()` or a similar method,
     *                       or a negative error code from `HTTPClient` itself.
     * @return `true` if the error code suggests a retry might be successful and the retry limit has not been reached.
     * @return `false` otherwise (e.g., for success codes, non-retryable client/server errors, or if max retries exceeded).
     */
    bool isRetryableError(int httpStatusCode);

// Suppress GCC warnings for deprecated declarations if using an older ArduinoJson version
// where StaticJsonDocument might trigger such warnings. For ArduinoJson v6+, StaticJsonDocument is standard.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    /**
     * @brief A reusable `StaticJsonDocument` for parsing JSON responses from HTTP requests.
     *
     * The size of this document is determined by `JSON_DOC_SIZE_API_RESPONSE` (or a similar
     * constant like `JSON_DOC_SIZE_DEVICE_CONFIG` if more specific) defined in `config.h`.
     * It is crucial that this size is sufficient to hold the largest JSON response expected
     * from any API endpoint this `WiFiManager` will interact with.
     * Using `StaticJsonDocument` helps prevent heap fragmentation on memory-constrained
     * microcontrollers like the ESP32 by pre-allocating the necessary memory, typically
     * in the global/static data segment when it's a class member as it is here.
     * The document is cleared (`_jsonDoc.clear()`) before attempting to parse each new response.
     */
    StaticJsonDocument<JSON_DOC_SIZE_DEVICE_CONFIG> _jsonDoc; // Consider renaming JSON_DOC_SIZE_DEVICE_CONFIG to a more general JSON_DOC_SIZE_API_RESPONSE if this manager handles diverse API responses.
#pragma GCC diagnostic pop
};
 
#endif // WIFI_MANAGER_H