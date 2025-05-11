#ifndef NETWORK_FACADE_H
#define NETWORK_FACADE_H

#include "NetworkInterface.h" // Already includes <functional> and <ArduinoJson.h>
#include <memory> // For std::unique_ptr
#include <functional> // Explicit include for std::function
#include <ArduinoJson.h> // Explicit include for JsonDocument
#include "DeviceState.h" // For access to fail safe mode status
#include "config.h" // For NETWORK_MAX_RESPONSE_LEN, WIFI_MAX_SSID_LEN, etc.
 
 // Forward declarations
class WiFiManager;
class GPRSManager;
// class TinyGsm; // Remove forward declaration, full def will come via GPRSManager.h -> TinyGsmClient.h
class LCDDisplay;
// class JsonDocument; // No longer needed, ArduinoJson.h is included

/**
 * @brief Facade for managing multiple network interfaces (e.g., WiFi, GPRS), implementing the `NetworkInterface`.
 *
 * This class acts as a unified front-end for network operations. It delegates tasks
 * to an active underlying network manager (`WiFiManager` or `GPRSManager`) based on a
 * specified `NetworkPreference` and the current availability and status of these interfaces.
 * The facade can take ownership of the manager instances (via `std::unique_ptr`) or
 * use externally managed raw pointers, providing flexibility in how network resources are handled.
 * It interacts with a `DeviceState` object to be aware of system-wide states like fail-safe mode.
 */
class NetworkFacade : public NetworkInterface {
public:
    /**
     * @brief Defines the preference strategy for selecting and using available network interfaces.
     * This determines which interface (WiFi or GPRS) is prioritized and whether fallback is attempted.
     */
    enum class NetworkPreference {
        WIFI_ONLY,      ///< Attempt to use only the WiFi interface. GPRS will not be used.
        GPRS_ONLY,      ///< Attempt to use only the GPRS interface. WiFi will not be used.
        WIFI_PREFERRED, ///< Prioritize WiFi. If WiFi connection fails or is unavailable, attempt to fallback to GPRS.
        GPRS_PREFERRED  ///< Prioritize GPRS. If GPRS connection fails or is unavailable, attempt to fallback to WiFi (less common scenario).
    };

    /**
     * @brief Constructs a NetworkFacade, taking ownership of the provided `WiFiManager` and `GPRSManager`
     *        instances via `std::unique_ptr`.
     * @param preference The preferred network interface strategy (e.g., `WIFI_PREFERRED`).
     * @param wifiManager A `std::unique_ptr` to a `WiFiManager` instance. The facade assumes ownership and
     *                    will manage its lifecycle. Can be `nullptr` if WiFi is not to be managed by this facade.
     * @param gprsManager A `std::unique_ptr` to a `GPRSManager` instance. The facade assumes ownership and
     *                    will manage its lifecycle. Can be `nullptr` if GPRS is not to be managed by this facade.
     * @param deviceState Pointer to a `DeviceState` object, used to check for system states like fail-safe mode
     *                    which might influence network behavior. Must not be `nullptr`.
     */
    NetworkFacade(
        NetworkPreference preference,
        std::unique_ptr<WiFiManager> wifiManager,
        std::unique_ptr<GPRSManager> gprsManager,
        DeviceState* deviceState
    );
 
    /**
     * @brief Constructs a NetworkFacade using externally managed (raw pointer) `WiFiManager` and `GPRSManager` instances.
     * This facade will **not** take ownership and will **not** delete these managers upon its own destruction.
     * The caller is responsible for the lifetime of the provided manager instances.
     * @param preference The preferred network interface strategy.
     * @param wifiManager Pointer to an existing `WiFiManager` instance. Must remain valid for the lifetime of the facade.
     *                    Can be `nullptr` if WiFi is not to be used.
     * @param gprsManager Pointer to an existing `GPRSManager` instance. Must remain valid for the lifetime of the facade.
     *                    Can be `nullptr` if GPRS is not to be used.
     * @param deviceState Pointer to a `DeviceState` object for fail-safe status checking. Must not be `nullptr`.
     */
    NetworkFacade(
        NetworkPreference preference,
        WiFiManager* wifiManager,
        GPRSManager* gprsManager,
        DeviceState* deviceState
    );
 
    /**
     * @brief Destructor.
     * If the facade owns the network managers (i.e., they were provided as `std::unique_ptr`s
     * in the constructor), their destructors will be called, releasing their resources.
     * If raw pointers were used, this destructor does nothing to the managed objects.
     */
    ~NetworkFacade() override;

    // Inherited from NetworkInterface
    /**
     * @brief Attempts to establish a network connection based on the current `_preference` and interface availability.
     *
     * It calls `determineActiveInterface()` to select the primary (and potentially fallback)
     * interface. Then, it invokes `connect()` on the chosen `_activeInterface`.
     * - If `_preference` is `WIFI_ONLY` or `GPRS_ONLY`, it attempts connection only on that interface.
     * - If `_preference` is `WIFI_PREFERRED`, it tries WiFi first. If WiFi fails and GPRS is available, it attempts GPRS.
     * - If `_preference` is `GPRS_PREFERRED`, it tries GPRS first. If GPRS fails and WiFi is available, it attempts WiFi.
     *
     * @return `true` if a connection is successfully established on an appropriate interface according to preference.
     * @return `false` if no connection could be established on any permissible interface.
     */
    bool connect() override;
    /**
     * @brief Disconnects the currently active network interface.
     *
     * If `_activeInterface` is not `nullptr`, its `disconnect()` method is called.
     * The `_activeInterface` is then set to `nullptr`.
     */
    void disconnect() override;
    /**
     * @brief Checks if the facade is currently connected through its active network interface.
     *
     * @return `true` if `_activeInterface` is not `nullptr` and `_activeInterface->isConnected()` returns `true`.
     * @return `false` otherwise (no active interface or the active one is not connected).
     */
    bool isConnected() const override;
    /**
     * @brief Initiates an asynchronous HTTP request.
     *
     * If the facade is not currently connected, it will first attempt to establish a connection
     * by calling its own `connect()` method (which respects the current `NetworkPreference`).
     * If a connection is active or successfully established, it then delegates the HTTP request
     * to the `_activeInterface->startAsyncHttpRequest()` method.
     *
     * @param url The target URL for the HTTP request.
     * @param method The HTTP method (e.g., "GET", "POST").
     * @param apiType A user-defined string categorizing the API call (for logging/debugging).
     * @param payload The request body (typically for POST requests, `nullptr` for GET).
     * @param cb The callback function `std::function<bool(JsonDocument& doc)>` to be invoked with the
     *           parsed JSON response.
     * @param needsAuth If `true`, an authorization token (if configured in the active manager) will be included.
     *
     * @return `true` if `_activeInterface` is valid, connected, and successfully initiated the request.
     * @return `false` if there's no active connected interface or the active interface failed to start the request.
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
     * @brief Updates the state of any ongoing asynchronous HTTP operations for the active network interface.
     *
     * This method must be called repeatedly in the main application loop. It delegates to
     * `_activeInterface->updateHttpOperations()` if `_activeInterface` is not `nullptr`.
     */
    void updateHttpOperations() override;
    /**
     * @brief Retrieves a human-readable status string from the active network interface.
     *
     * If `_activeInterface` is not `nullptr`, its `getStatusString()` is called, and the result is
     * prefixed (e.g., "WIFI: " or "GPRS: ") to indicate the source.
     * If no interface is active, a message like "No active network interface." is returned.
     *
     * @return `String` containing the current status of the active network connection or state of the facade.
     */
    String getStatusString() const override;

    // Additional methods specific to facade
    /**
     * @brief Attempts to explicitly switch the active network interface to WiFi.
     *
     * This method first tries to connect using the `_wifiManagerRaw`. If successful and
     * the `_gprsManagerRaw` was previously active and connected, GPRS will be disconnected.
     * The `_activeInterface` is updated accordingly via `determineActiveInterface()`.
     * The network preference `_preference` is not changed by this call.
     *
     * @return `true` if `_wifiManagerRaw` exists and its connection attempt is successful.
     * @return `false` if `_wifiManagerRaw` is `nullptr` or its connection attempt fails.
     */
    bool switchToWiFi();

    /**
     * @brief Attempts to explicitly switch the active network interface to GPRS.
     *
     * This method first tries to connect using the `_gprsManagerRaw`. If successful and
     * the `_wifiManagerRaw` was previously active and connected, WiFi will be disconnected.
     * The `_activeInterface` is updated accordingly via `determineActiveInterface()`.
     * The network preference `_preference` is not changed by this call.
     *
     * @return `true` if `_gprsManagerRaw` exists and its connection attempt is successful.
     * @return `false` if `_gprsManagerRaw` is `nullptr` or its connection attempt fails.
     */
    bool switchToGPRS();

    /**
     * @brief Gets a pointer to the currently active underlying network interface (`WiFiManager` or `GPRSManager`).
     * The active interface is determined by the `_preference` and current connection states, managed by
     * `determineActiveInterface()`.
     *
     * @return Pointer to the active `NetworkInterface` instance, or `nullptr` if no interface is currently
     *         selected or active (e.g., both unavailable or preference set to disallow them).
     */
    NetworkInterface* getCurrentInterface() const;

    /**
     * @brief Gets the current network preference strategy that dictates interface selection and fallback behavior.
     * @return The current `NetworkPreference` enum value (e.g., `WIFI_PREFERRED`).
     */
    NetworkPreference getPreference() const;

    /**
     * @brief Sets a new network preference strategy.
     *
     * After updating `_preference`, this method calls `determineActiveInterface()` to re-evaluate
     * and potentially switch the `_activeInterface` based on the new strategy and current
     * interface availability. This might involve disconnecting an old interface and connecting a new one.
     *
     * @param preference The new `NetworkPreference` to apply.
     */
    void setPreference(NetworkPreference preference);

    /**
     * @brief Retrieves a pointer to the `WiFiManager` instance managed by this facade, regardless of active status.
     * This allows for WiFi-specific operations or configurations not exposed through the generic `NetworkInterface`.
     *
     * @return Pointer to the `WiFiManager` (`_wifiManagerRaw`). Returns `nullptr` if no `WiFiManager`
     *         was provided or configured for this facade.
     */
    WiFiManager* getWiFiManager() const;

    /**
     * @brief Retrieves a pointer to the `GPRSManager` instance managed by this facade, regardless of active status.
     * This allows for GPRS-specific operations or configurations not exposed through the generic `NetworkInterface`.
     *
     * @return Pointer to the `GPRSManager` (`_gprsManagerRaw`). Returns `nullptr` if no `GPRSManager`
     *         was provided or configured for this facade.
     */
    GPRSManager* getGPRSManager() const;
 
    /**
     * @brief Checks if the system is currently operating in a fail-safe mode.
     * This status is read from the associated `_deviceState` object.
     * Network operations might be restricted or altered when fail-safe mode is active.
     *
     * @return `true` if `_deviceState` is not `nullptr` and indicates that fail-safe mode is active.
     * @return `false` otherwise (including if `_deviceState` is `nullptr`).
     */
    bool isSafeModeActive() const;

private:
    NetworkPreference _preference; ///< The configured strategy for selecting network interfaces (e.g., WiFi only, WiFi preferred with GPRS fallback).
    std::unique_ptr<WiFiManager> _wifiManagerOwned; ///< Manages the `WiFiManager` if its lifetime is owned by this facade (passed via `std::unique_ptr` in constructor). Will be `nullptr` if `WiFiManager` is externally managed.
    std::unique_ptr<GPRSManager> _gprsManagerOwned; ///< Manages the `GPRSManager` if its lifetime is owned by this facade (passed via `std::unique_ptr` in constructor). Will be `nullptr` if `GPRSManager` is externally managed.
    WiFiManager* _wifiManagerRaw; ///< Raw pointer to the `WiFiManager` instance. This is used for all operations, regardless of ownership. Points to the managed object if `_wifiManagerOwned` is set, or to the externally provided pointer.
    GPRSManager* _gprsManagerRaw; ///< Raw pointer to the `GPRSManager` instance. Used for all operations, regardless of ownership. Points to the managed object if `_gprsManagerOwned` is set, or to the externally provided pointer.
    DeviceState* _deviceState;    ///< Pointer to a shared `DeviceState` object. Used to query system-wide states like fail-safe mode, which can influence network strategy. Must not be null.
    // char _apiResponse[NETWORK_MAX_RESPONSE_LEN]; // Removed: This buffer was initialized but not used by the facade. Response handling is delegated.

    NetworkInterface* _activeInterface; ///< Pointer to the currently selected and active network interface (either `_wifiManagerRaw` or `_gprsManagerRaw`). It is `nullptr` if no interface is currently active.

    /**
     * @brief Selects and sets the `_activeInterface` based on the current `_preference`,
     *        and the availability and reported connection status of `_wifiManagerRaw` and `_gprsManagerRaw`.
     *
     * This core internal method implements the logic for choosing which interface to use.
     * It is invoked during initialization, when `setPreference()` is called, or potentially
     * after connection attempts to handle fallback logic as defined by `_preference`.
     * For example, if `_preference` is `WIFI_PREFERRED` and WiFi is available, `_activeInterface`
     * will be set to `_wifiManagerRaw`. If WiFi subsequently fails to connect and GPRS is available,
     * this method might then set `_activeInterface` to `_gprsManagerRaw`.
     */
    void determineActiveInterface();
};

#endif // NETWORK_FACADE_H