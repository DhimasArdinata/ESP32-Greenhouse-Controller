#include "WiFiManager.h"
#include "config.h" // For DEBUG_PRINTLN and potentially other configs
#include <Arduino.h>  // For millis(), Serial, etc.
#include <esp_task_wdt.h> // For watchdog reset

// Constructor
WiFiManager::WiFiManager(const char* ssid, const char* password, const char* authToken, LCDDisplay* lcd)
    : _ssid(ssid),
      _password(password),
      _authToken(authToken),
      _lcd(lcd),
      _currentHttpState(WiFiHttpState::IDLE),
      _asyncOperationActive(false),
      _httpStatusCode(0) {
    // Ensure JsonDocument has enough capacity. Adjust as needed.
    // For ESP32, default capacity of DynamicJsonDocument might be okay,
    // but for static JsonDocument, you need to specify.
    // Let's assume a reasonable default or it's handled by ArduinoJson's defaults for now.
    // _jsonDoc.reserve(1024); // Example if using DynamicJsonDocument or need to ensure capacity for static
}

WiFiManager::~WiFiManager() {
    if (_httpClient.connected()) {
        _httpClient.end();
    }
}

void WiFiManager::setCredentials(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
}

void WiFiManager::setAuthToken(const char* authToken) {
    _authToken = authToken;
}

// Renamed from connect() to connectWiFi() and added retry logic
bool WiFiManager::connectWiFi() {
    if (_ssid.length() == 0) {
        DEBUG_PRINTLN(1, "WiFiManager: No SSID configured.");
        return false;
    }

    const int MAX_CONNECT_RETRIES = 2; // 1 initial attempt + 2 retries
    const unsigned long CONNECT_TIMEOUT_MS = 20000; // 20 seconds per attempt

    for (int attempt = 0; attempt <= MAX_CONNECT_RETRIES; ++attempt) {
        DEBUG_PRINTF(3, "WiFiManager: Connecting to %s (Attempt %d/%d)...\n", _ssid.c_str(), attempt + 1, MAX_CONNECT_RETRIES + 1);
        if (_lcd) {
            // LCD updates are handled by the main application using getStatusString()
        }

        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true); // Disconnect previous connection, if any
        delay(100); // Short delay before begin
        WiFi.begin(_ssid.c_str(), _password.c_str());

        unsigned long startTime = millis();
        bool connectedThisAttempt = false;
        while (millis() - startTime < CONNECT_TIMEOUT_MS) {
            esp_task_wdt_reset();
            if (WiFi.status() == WL_CONNECTED) {
                connectedThisAttempt = true;
                break;
            }
            DEBUG_PRINTLN(4, "WiFiManager: Waiting for connection...");
            delay(500);
        }

        if (connectedThisAttempt) {
            DEBUG_PRINTF(3, "WiFiManager: Connected. IP: %s\n", WiFi.localIP().toString().c_str());
            if (_lcd) {
                // LCD updates by main app
            }
            return true;
        } else {
            DEBUG_PRINTF(1, "WiFiManager: Connection attempt %d timed out.\n", attempt + 1);
            WiFi.disconnect(true); // Ensure it's off before next attempt or exiting
            if (attempt < MAX_CONNECT_RETRIES) {
                DEBUG_PRINTLN(2, "WiFiManager: Retrying connection...");
                delay(1000); // Wait a bit before retrying
            }
        }
    }

    DEBUG_PRINTLN(1, "WiFiManager: All connection attempts failed.");
    return false;
}

// Public connect method now calls the internal connectWiFi with retries
bool WiFiManager::connect() {
    return connectWiFi();
}

void WiFiManager::disconnect() {
    DEBUG_PRINTLN(3, "WiFiManager: Disconnecting...");
    WiFi.disconnect(true);
    delay(100); // Allow time for disconnection
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isActuallyConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIPAddress() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

String WiFiManager::getStatusString() const {
    char buffer[64]; // Increased buffer size
    if (isConnected()) {
        snprintf(buffer, sizeof(buffer), "WiFi: Connected (%s)", WiFi.localIP().toString().c_str());
        return String(buffer);
    }
    // For "WiFi: Disconnected", snprintf is overkill, but for consistency:
    snprintf(buffer, sizeof(buffer), "WiFi: Disconnected");
    return String(buffer);
}

bool WiFiManager::startAsyncHttpRequest(
    const char* url,
    const char* method,
    const char* apiType,
    const char* payload,
    std::function<bool(JsonDocument& doc)> cb,
    bool needsAuth) {

    if (_asyncOperationActive) {
        DEBUG_PRINTF(2, "WiFiManager: Async HTTP operation already active. Request '%s' ignored.\n", apiType);
        return false;
    }
    if (!isConnected()) {
        DEBUG_PRINTF(1, "WiFiManager: Not connected. Request '%s' failed.\n", apiType);
        return false;
    }

    DEBUG_PRINTF(3, "WiFiManager: Starting Async HTTP %s for '%s' to %s\n", method, apiType, url);

    _asyncUrl = url;
    _asyncMethod = method;
    _asyncApiType = apiType;
    _asyncPayload = (payload ? payload : "");
    _asyncCb = cb;
    _asyncNeedsAuth = needsAuth;
    _asyncRequestStartTime = millis();
    _asyncOperationActive = true;
    _httpStatusCode = 0;
    _httpRetries = 0; // Initialize retry counter
    _jsonDoc.clear(); // Clear the document for the new request

    _currentHttpState = WiFiHttpState::BEGIN_REQUEST;
    return true;
}

void WiFiManager::updateHttpOperations() {
    if (!_asyncOperationActive) {
        return;
    }
    esp_task_wdt_reset();

    // Basic timeout for the whole operation
    if (millis() - _asyncRequestStartTime > 30000) { // 30-second overall timeout
        DEBUG_PRINTF(1, "WiFiManager: Async HTTP operation for '%s' timed out.\n", _asyncApiType.c_str());
        if (_httpClient.connected()) _httpClient.end();
        _currentHttpState = WiFiHttpState::ERROR;
    }

    bool cbOk = false; // Declare cbOk before the switch

    switch (_currentHttpState) {
        case WiFiHttpState::IDLE:
            // Should not be in this state if _asyncOperationActive is true
            _asyncOperationActive = false;
            break;

        case WiFiHttpState::BEGIN_REQUEST:
            DEBUG_PRINTF(4, "WiFiManager Async (%s): http.begin()\n", _asyncApiType.c_str());
            // _wifiClientInstance needs to be valid. HTTPClient uses it.
            if (_httpClient.begin(_wifiClientInstance, _asyncUrl)) {
                if (_asyncNeedsAuth && _authToken.length() > 0) {
                    // Bearer token construction:
                    char authHeaderValue[128]; // Buffer for "Bearer <token>"
                    snprintf(authHeaderValue, sizeof(authHeaderValue), "Bearer %s", _authToken.c_str());
                    _httpClient.addHeader("Authorization", authHeaderValue);
                }
                if (_asyncPayload.length() > 0 && (_asyncMethod == "POST" || _asyncMethod == "PUT" || _asyncMethod == "PATCH")) {
                    _httpClient.addHeader("Content-Type", "application/json");
                }
                _httpClient.setReuse(false); // Good practice for ESP32 HTTPClient
                _httpClient.setTimeout(15000); // Set timeout for this specific request
                _currentHttpState = WiFiHttpState::SENDING_REQUEST;
            } else {
                DEBUG_PRINTF(1, "WiFiManager Async (%s) Err: http.begin() failed.\n", _asyncApiType.c_str());
                _currentHttpState = WiFiHttpState::ERROR;
            }
            break;

        case WiFiHttpState::SENDING_REQUEST:
            DEBUG_PRINTF(4, "WiFiManager Async (%s): Sending %s\n", _asyncApiType.c_str(), _asyncMethod.c_str());
            if (_asyncMethod == "GET") {
                _httpStatusCode = _httpClient.GET();
            } else if (_asyncMethod == "POST") {
                _httpStatusCode = _httpClient.POST(_asyncPayload);
            } else {
                DEBUG_PRINTF(1, "WiFiManager Async (%s) Err: Unsupported method %s\n", _asyncApiType.c_str(), _asyncMethod.c_str());
                _currentHttpState = WiFiHttpState::ERROR;
                break;
            }

            if (_httpStatusCode > 0) { // HTTPClient returned a code (success or error)
                DEBUG_PRINTF(3, "WiFiManager Async (%s): Status %d\n", _asyncApiType.c_str(), _httpStatusCode);
                _currentHttpState = WiFiHttpState::PROCESSING_RESPONSE;
            } else if (_httpStatusCode < 0) { // An error occurred with HTTPClient
                DEBUG_PRINTF(1, "WiFiManager Async (%s) Err: Code %d (%s)\n", _asyncApiType.c_str(), _httpStatusCode, _httpClient.errorToString(_httpStatusCode).c_str());
                _currentHttpState = WiFiHttpState::ERROR;
            }
            // If _httpStatusCode is 0, it means HTTP_CODE_WAITING_FOR_RESPONSE,
            // but standard ESP32 HTTPClient GET/POST are blocking until headers or error.
            // So, we typically expect a non-zero code here.
            break;

        case WiFiHttpState::PROCESSING_RESPONSE:
            DEBUG_PRINTF(4, "WiFiManager Async (%s): Processing response.\n", _asyncApiType.c_str());
            // bool cbOk = false; // Moved before switch
            if (_httpStatusCode >= 200 && _httpStatusCode < 300) {
                if (_asyncCb) {
                    String responsePayload = _httpClient.getString(); // Still uses String here, acceptable for one-time read
                    DeserializationError err = deserializeJson(_jsonDoc, responsePayload);
                    if (err) {
                        DEBUG_PRINTF(1, "WiFiManager Async (%s): JSON Deserialization failed: %s\n", _asyncApiType.c_str(), err.c_str());
                        DEBUG_PRINTF(4, "Response was: %s\n", responsePayload.c_str());
                    } else {
                        cbOk = _asyncCb(_jsonDoc);
                        if (!cbOk) {
                             DEBUG_PRINTF(2, "WiFiManager Async (%s): Callback processing failed.\n", _asyncApiType.c_str());
                        }
                    }
                } else { // No callback, but 2xx status is success for the HTTP op itself
                    cbOk = true;
                }
            } else { // HTTP error code
                String httpResponse = _httpClient.getString(); // Read response for logging
                DEBUG_PRINTF(1, "WiFiManager Async (%s): HTTP Error Status %d. Response: %s\n", _asyncApiType.c_str(), _httpStatusCode, httpResponse.c_str());
            }
            _httpClient.end(); // IMPORTANT: Always end the client connection
            _currentHttpState = cbOk ? WiFiHttpState::COMPLETE : WiFiHttpState::ERROR;
            break;

        case WiFiHttpState::RETRY_WAIT:
            if (millis() >= _asyncRequestStartTime) { // Check if delay has passed
                DEBUG_PRINTF(2, "WiFiManager Async (%s): Retry delay complete. Attempting retry %d.\n", _asyncApiType.c_str(), _httpRetries);
                // Reset relevant HTTP state variables before retrying
                _httpStatusCode = 0;
                _jsonDoc.clear();
                _asyncRequestStartTime = millis(); // Reset start time for the new attempt's timeout
                _currentHttpState = WiFiHttpState::BEGIN_REQUEST; // Start retry
            }
            // Else, continue waiting
            break;

        case WiFiHttpState::COMPLETE:
            DEBUG_PRINTF(3, "WiFiManager Async (%s): Operation complete.\n", _asyncApiType.c_str());
            if (_httpClient.connected()) _httpClient.end(); // Ensure client is closed on success
            _asyncOperationActive = false;
            _currentHttpState = WiFiHttpState::IDLE;
            break;

        case WiFiHttpState::ERROR:
            // Note: _httpClient.end() should have been called in PROCESSING_RESPONSE before transitioning here
            // or if an error occurred before/during SENDING_REQUEST.
            // If it's an error from BEGIN_REQUEST (e.g. http.begin failed), then client might not be "connected".
            // For safety, we can call _httpClient.end() again, it should be safe.
            if (_httpClient.connected()) _httpClient.end();

            if (isRetryableError(_httpStatusCode) && _httpRetries < MAX_HTTP_RETRIES) {
                _httpRetries++;
                DEBUG_PRINTF(2, "WiFiManager Async (%s): Retryable error (%d). Retrying in %lu ms (attempt %d).\n", _asyncApiType.c_str(), _httpStatusCode, HTTP_RETRY_DELAY_MS, _httpRetries);
                _asyncRequestStartTime = millis() + HTTP_RETRY_DELAY_MS; // Set start time for delay
                _currentHttpState = WiFiHttpState::RETRY_WAIT; // Go to a wait state before retry
                // _asyncOperationActive remains true
            } else {
                if (!isRetryableError(_httpStatusCode)) {
                    DEBUG_PRINTF(1, "WiFiManager Async (%s): Non-retryable HTTP error %d. Final failure.\n", _asyncApiType.c_str(), _httpStatusCode);
                } else { // Max retries reached for a retryable error
                    DEBUG_PRINTF(1, "WiFiManager Async (%s): Max HTTP retries reached for error %d. Final failure.\n", _asyncApiType.c_str(), _httpStatusCode);
                }
                _asyncOperationActive = false;
                _currentHttpState = WiFiHttpState::IDLE;
            }
            break;

        default:
            DEBUG_PRINTF(1, "WiFiManager Async (%s): Unhandled state %d\n", _asyncApiType.c_str(), (int)_currentHttpState);
            if (_httpClient.connected()) _httpClient.end();
            _currentHttpState = WiFiHttpState::ERROR; // Go to error state, then IDLE
            _asyncOperationActive = false;
            break;
    }
}

// The WiFiManager::printDebug method has been removed.
// Global DEBUG_PRINTF or DEBUG_PRINTLN macros are used directly.
bool WiFiManager::isRetryableError(int httpStatusCode) {
    // HTTP status code <= 0 from HTTPClient often indicates a connection or client-side error
    // (e.g., HTTPC_ERROR_CONNECTION_REFUSED, HTTPC_ERROR_SEND_HEADER_FAILED, HTTPC_ERROR_CONNECTION_LOST, HTTPC_ERROR_TIMEOUT)
    if (httpStatusCode <= 0) { 
        return true;
    }
    // Specific 4xx errors that might be retryable
    if (httpStatusCode == 408) { // Request Timeout
        return true;
    }
    if (httpStatusCode == 429) { // Too Many Requests (could be due to rate limiting)
        return true;
    }
    // All 5xx server-side errors are generally considered retryable
    if (httpStatusCode >= 500 && httpStatusCode <= 599) {
        return true;
    }
    // Most other 4xx errors (400, 401, 403, 404, etc.) are typically client errors and shouldn't be retried with the same request.
    // 2xx followed by callback returning false is also not retryable at HTTP level.
    return false;
}