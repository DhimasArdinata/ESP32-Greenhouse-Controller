#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H

#include <functional> // For std::function
#include <ArduinoJson.h> // For JsonDocument

// class JsonDocument; // No longer needed, full include above

/**
 * @brief Abstract base class defining the interface for network management.
 *
 * This class provides a common set of operations for different network
 * connectivity types (e.g., WiFi, GPRS). Derived classes must implement
 * these pure virtual functions.
 */
class NetworkInterface {
public:
    /**
     * @brief Virtual destructor for proper cleanup of derived classes.
     */
    virtual ~NetworkInterface() {}

    /**
     * @brief Attempts to connect to the network.
     * @return true if connection is successful, false otherwise.
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnects from the network.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Checks if currently connected to the network.
     * @return true if connected, false otherwise.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Initiates an asynchronous HTTP request.
     *
     * @param url The target URL for the request.
     * @param method HTTP method (e.g., "GET", "POST").
     * @param apiType A string descriptor for the type of API call (for logging/debugging).
     * @param payload The request body (e.g., JSON string for POST). Null or empty for GET.
     * @param cb Callback function to process the JSON response.
     *           Takes a JsonDocument&, returns true if processing was successful.
     * @param needsAuth Boolean indicating if the request requires an authorization header. Defaults to true.
     * @return true if the request was successfully initiated, false otherwise (e.g., another operation active, not connected).
     */
    virtual bool startAsyncHttpRequest(
        const char* url,
        const char* method,
        const char* apiType,
        const char* payload,
        std::function<bool(JsonDocument& doc)> cb,
        bool needsAuth = true
    ) = 0;

    /**
     * @brief Processes any ongoing asynchronous HTTP operations.
     * This method should be called repeatedly from the main loop to drive the state
     * machine of asynchronous requests.
     */
    virtual void updateHttpOperations() = 0;

    /**
     * @brief Provides a general status string for the network interface.
     * Useful for display purposes (e.g., on an LCD).
     * @return String containing the current status.
     */
    virtual String getStatusString() const = 0;
};

#endif // NETWORK_INTERFACE_H