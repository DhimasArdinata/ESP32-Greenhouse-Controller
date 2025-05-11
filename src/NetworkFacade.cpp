#include "NetworkFacade.h"
#include "WiFiManager.h"   // Ensure full definition is available
#include "GPRSManager.h"   // Ensure full definition is available
#include "config.h"        // For DEBUG_PRINTLN
#include <Arduino.h>       // For String, Serial, etc.

/**
* @brief Constructs a NetworkFacade, taking ownership of the provided managers.
* Refer to NetworkFacade.h for detailed documentation.
*/
NetworkFacade::NetworkFacade(
   NetworkPreference preference,
    std::unique_ptr<WiFiManager> wifiManager,
    std::unique_ptr<GPRSManager> gprsManager,
    DeviceState* deviceState) // Added deviceState
    : _preference(preference),
      _wifiManagerOwned(std::move(wifiManager)),
      _gprsManagerOwned(std::move(gprsManager)),
      _wifiManagerRaw(_wifiManagerOwned.get()),
      _gprsManagerRaw(_gprsManagerOwned.get()),
      _deviceState(deviceState), // Initialize _deviceState
      _activeInterface(nullptr) {
   // _apiResponse[0] = '\0'; // Removed: _apiResponse member is removed from header
   DEBUG_PRINTLN(3, "NetworkFacade (owned): Initialized.");
   determineActiveInterface(); // Initial determination
}

/**
* @brief Constructs a NetworkFacade using externally managed (raw pointer) managers.
* Refer to NetworkFacade.h for detailed documentation.
*/
NetworkFacade::NetworkFacade(
   NetworkPreference preference,
   WiFiManager* wifiManager,
    GPRSManager* gprsManager,
    DeviceState* deviceState) // Added deviceState
    : _preference(preference),
      _wifiManagerOwned(nullptr), // Not owning
      _gprsManagerOwned(nullptr), // Not owning
      _wifiManagerRaw(wifiManager),
      _gprsManagerRaw(gprsManager),
      _deviceState(deviceState), // Initialize _deviceState
      _activeInterface(nullptr) {
   // _apiResponse[0] = '\0'; // Removed: _apiResponse member is removed from header
   DEBUG_PRINTLN(3, "NetworkFacade (raw ptrs): Initialized.");
   determineActiveInterface(); // Initial determination
}


/**
* @brief Destructor for NetworkFacade.
* Handles cleanup of owned resources.
* Refer to NetworkFacade.h for detailed documentation.
*/
NetworkFacade::~NetworkFacade() {
   DEBUG_PRINTLN(3, "NetworkFacade: Shutting down.");
    // unique_ptr will handle deletion if owned.
    // If raw pointers were used, caller is responsible for deletion.
    _activeInterface = nullptr; // Just to be safe
}

/**
* @brief Determines and sets the `_activeInterface` based on the current `_preference`
*        and connection statuses of the available WiFi and GPRS managers.
*
* This private helper method is central to the facade's logic for selecting which
* underlying network interface should be used for operations. It considers the
* configured `NetworkPreference` (e.g., WIFI_ONLY, WIFI_PREFERRED) and the current
* `isConnected()` state of each potential manager.
*
* - For `WIFI_ONLY` or `GPRS_ONLY`, it selects the specified manager if available.
* - For `WIFI_PREFERRED`, it prioritizes a connected WiFi manager. If WiFi is not
*   connected but GPRS is, GPRS is chosen. If neither is connected, it defaults to
*   the WiFi manager if available, otherwise GPRS if available.
* - For `GPRS_PREFERRED`, the logic is symmetrical to `WIFI_PREFERRED`.
*
* After selection, `_activeInterface` will point to the chosen `NetworkInterface`
* (either `_wifiManagerRaw` or `_gprsManagerRaw`), or `nullptr` if no suitable
* interface can be determined (e.g., preference is WIFI_ONLY but no WiFiManager is present).
*/
void NetworkFacade::determineActiveInterface() {
   _activeInterface = nullptr; // Start fresh

    WiFiManager* wm = getWiFiManager();
    GPRSManager* gm = getGPRSManager();

    bool wifiConnected = (wm && wm->isConnected());
    bool gprsConnected = (gm && gm->isConnected());

    DEBUG_PRINTF(4, "NetworkFacade: Determining active interface. WiFi: %d, GPRS: %d, Pref: %d\n", wifiConnected, gprsConnected, (int)_preference);


    switch (_preference) {
        case NetworkPreference::WIFI_ONLY:
            if (wm) _activeInterface = wm;
            break;
        case NetworkPreference::GPRS_ONLY:
            if (gm) _activeInterface = gm;
            break;
        case NetworkPreference::WIFI_PREFERRED:
            if (wm && wifiConnected) _activeInterface = wm;
            else if (gm && gprsConnected) _activeInterface = gm;
            else if (wm) _activeInterface = wm; // Default to trying WiFi if nothing is connected
            else if (gm) _activeInterface = gm;
            break;
        case NetworkPreference::GPRS_PREFERRED:
            if (gm && gprsConnected) _activeInterface = gm;
            else if (wm && wifiConnected) _activeInterface = wm;
            else if (gm) _activeInterface = gm; // Default to trying GPRS
            else if (wm) _activeInterface = wm;
            break;
    }
    if (_activeInterface) {
        // _activeInterface->getStatusString() still returns String. We'll use its c_str()
        DEBUG_PRINTF(3, "NetworkFacade: Active interface set to %s\n", _activeInterface->getStatusString().c_str());
    } else {
        DEBUG_PRINTLN(2, "NetworkFacade: No active interface could be determined.");
    }
}


/**
* @brief Attempts to establish a network connection based on the current preference.
* This method implements the connection logic considering `_preference`.
* It tries the preferred interface first, and if applicable (e.g., WIFI_PREFERRED and WiFi fails),
* it attempts to connect via the fallback interface.
* After connection attempts, it calls `determineActiveInterface()` to update the active status.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::connect() {
   DEBUG_PRINTLN(3, "NetworkFacade: connect() called.");
    WiFiManager* wm = getWiFiManager();
    GPRSManager* gm = getGPRSManager();
    bool success = false;

    switch (_preference) {
        case NetworkPreference::WIFI_ONLY:
            if (wm) success = wm->connect();
            break;
        case NetworkPreference::GPRS_ONLY:
            if (gm) success = gm->connect();
            break;
        case NetworkPreference::WIFI_PREFERRED:
            if (wm && wm->connect()) {
                success = true;
            } else if (gm) {
                DEBUG_PRINTLN(3, "NetworkFacade: WiFi failed or not available, trying GPRS.");
                if (wm && wm->isConnected()) wm->disconnect(); // Ensure WiFi is off if GPRS is to be primary
                success = gm->connect();
            }
            break;
        case NetworkPreference::GPRS_PREFERRED:
            if (gm && gm->connect()) {
                success = true;
            } else if (wm) {
                DEBUG_PRINTLN(3, "NetworkFacade: GPRS failed or not available, trying WiFi.");
                if (gm && gm->isConnected()) gm->disconnect(); // Ensure GPRS is off
                success = wm->connect();
            }
            break;
    }
    determineActiveInterface(); // Update active interface based on connection result
    return success;
}

/**
* @brief Disconnects all underlying network interfaces (WiFi and GPRS).
* It iterates through available managers and calls their `disconnect()` method.
* `_activeInterface` is set to `nullptr` as no interface remains active.
* Refer to NetworkFacade.h for detailed documentation.
*/
void NetworkFacade::disconnect() {
   DEBUG_PRINTLN(3, "NetworkFacade: disconnect() called.");
    WiFiManager* wm = getWiFiManager();
    GPRSManager* gm = getGPRSManager();

    if (wm && wm->isConnected()) {
        wm->disconnect();
    }
    if (gm && gm->isConnected()) {
        gm->disconnect();
    }
    _activeInterface = nullptr; // No active interface after explicit disconnect
}

/**
* @brief Checks if the facade is currently connected through its active network interface.
* It primarily checks the `isConnected()` status of the `_activeInterface`.
* As a fallback, if `_activeInterface` is null, it checks the individual managers.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::isConnected() const {
   // Check the explicitly set _activeInterface first
   if (_activeInterface) {
       return _activeInterface->isConnected();
   }
   // Fallback: check any available interface if _activeInterface is somehow null.
   // This part might be less critical if determineActiveInterface is consistently called.
   WiFiManager* wm = getWiFiManager();
   GPRSManager* gm = getGPRSManager();
   if (wm && wm->isConnected()) {
       // _activeInterface = wm; // Removed: Cannot assign in const method. determineActiveInterface() should handle this.
       return true;
   }
   if (gm && gm->isConnected()) {
       // _activeInterface = gm; // Removed: Cannot assign in const method. determineActiveInterface() should handle this.
       return true;
   }
   return false;
}

/**
* @brief Initiates an asynchronous HTTP request.
* If not currently connected, it first attempts to `connect()` based on preferences.
* If a connection is active or established, it delegates the request to the `_activeInterface`.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::startAsyncHttpRequest(
   const char* url,
   const char* method,
   const char* apiType,
   const char* payload,
   std::function<bool(JsonDocument& doc)> cb,
   bool needsAuth) {

   if (!isConnected()) { // Check overall facade connectivity
       DEBUG_PRINTLN(3, "NetworkFacade: Not connected. Attempting to connect before HTTP request.");
       if (!connect()) { // connect() handles preferences and sets _activeInterface
           DEBUG_PRINTF(1, "NetworkFacade: Connection failed for %s. HTTP request cannot proceed.\n", apiType);
           return false;
       }
       // At this point, if connect() succeeded, _activeInterface should be set and connected.
   }

   // By now, _activeInterface should be valid and connected if connect() was successful or if it was already connected.
   if (_activeInterface && _activeInterface->isConnected()) {
       return _activeInterface->startAsyncHttpRequest(url, method, apiType, payload, cb, needsAuth);
   } else {
       // This case implies that even after an attempt to connect(), no interface is active and connected.
       DEBUG_PRINTF(1, "NetworkFacade: No active/connected interface available for HTTP request for %s even after connection attempt.\n", apiType);
       return false;
   }
}

/**
* @brief Updates ongoing asynchronous HTTP operations for the active interface.
* This method should be called periodically to process HTTP responses.
* It delegates the call to the `_activeInterface` if one exists.
* Refer to NetworkFacade.h for detailed documentation.
*/
void NetworkFacade::updateHttpOperations() {
    // Update operations for ALL interfaces, as one might be completing
    // a request even if it's not the "active" one for new requests.
    // Or, more simply, only update the _activeInterface.
    // For now, let's assume only the _activeInterface handles ongoing operations.
    if (_activeInterface) {
        _activeInterface->updateHttpOperations();
    }
    // If requests could be started on one, then switched, both would need update.
    // This simplified version assumes an op is tied to the interface it started on.
    // And that _activeInterface is the one currently handling any ops.
}

/**
* @brief Retrieves a comprehensive status string for the NetworkFacade.
* The string includes the status of the active interface, or details about
* available interfaces and current preference if disconnected.
* Refer to NetworkFacade.h for detailed documentation.
*/
String NetworkFacade::getStatusString() const {
   char buffer[128]; //Reasonable buffer size for status strings

    if (_activeInterface) {
        // _activeInterface->getStatusString() returns String. Use its c_str()
        snprintf(buffer, sizeof(buffer), "Facade (Active: %s)", _activeInterface->getStatusString().c_str());
        return String(buffer);
    }

    WiFiManager* wm = getWiFiManager();
    GPRSManager* gm = getGPRSManager();
    bool wifiAvailable = (wm != nullptr);
    bool gprsAvailable = (gm != nullptr);

    if (wifiAvailable && wm && wm->isConnected()) {
        // wm->getStatusString() returns String. Use its c_str()
        snprintf(buffer, sizeof(buffer), "Facade (WiFi Connected: %s)", wm->getStatusString().c_str());
        return String(buffer);
    }
    if (gprsAvailable && gm && gm->isConnected()) {
        // gm->getStatusString() returns String. Use its c_str()
        snprintf(buffer, sizeof(buffer), "Facade (GPRS Connected: %s)", gm->getStatusString().c_str());
        return String(buffer);
    }
    
    const char* prefStr = "";
    switch (_preference) {
        case NetworkPreference::WIFI_ONLY: prefStr = "WiFi Only"; break;
        case NetworkPreference::GPRS_ONLY: prefStr = "GPRS Only"; break;
        case NetworkPreference::WIFI_PREFERRED: prefStr = "WiFi Preferred"; break;
        case NetworkPreference::GPRS_PREFERRED: prefStr = "GPRS Preferred"; break;
    }
    snprintf(buffer, sizeof(buffer), "Facade (Disconnected. Pref: %s. WiFi Avail: %d, GPRS Avail: %d)",
             prefStr, wifiAvailable, gprsAvailable);
    return String(buffer);
}

/**
* @brief Explicitly attempts to switch to and connect via the WiFi interface.
* If GPRS is active, it will be disconnected *after* WiFi successfully connects.
* Calls `determineActiveInterface()` to update the active state.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::switchToWiFi() {
   DEBUG_PRINTLN(3, "NetworkFacade: Attempting to switch to WiFi.");
    WiFiManager* wm = getWiFiManager();
    GPRSManager* gm = getGPRSManager();

    if (!wm) {
        DEBUG_PRINTLN(1, "NetworkFacade: WiFiManager not available for switching.");
        return false;
    }

    DEBUG_PRINTLN(4, "NetworkFacade: Trying to connect WiFi before potentially disconnecting GPRS.");
    if (wm->connect()) {
        DEBUG_PRINTLN(3, "NetworkFacade: WiFi connected successfully during switch attempt.");
        if (gm && gm->isConnected()) {
            DEBUG_PRINTLN(3, "NetworkFacade: Disconnecting GPRS as WiFi is now active.");
            gm->disconnect();
        }
        determineActiveInterface(); // Set WiFi as active
        return true;
    } else {
        DEBUG_PRINTLN(2, "NetworkFacade: WiFi connection failed during switch attempt. GPRS (if active) will not be disconnected.");
        // Ensure the active interface is correctly set, possibly back to GPRS if it was active and WiFi failed
        determineActiveInterface();
        return false;
    }
}

/**
* @brief Explicitly attempts to switch to and connect via the GPRS interface.
* If WiFi is active, it will be disconnected *after* GPRS successfully connects.
* Calls `determineActiveInterface()` to update the active state.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::switchToGPRS() {
   DEBUG_PRINTLN(3, "NetworkFacade: Attempting to switch to GPRS.");
    GPRSManager* gm = getGPRSManager();
    WiFiManager* wm = getWiFiManager();

    if (!gm) {
        DEBUG_PRINTLN(1, "NetworkFacade: GPRSManager not available for switching.");
        return false;
    }

    DEBUG_PRINTLN(4, "NetworkFacade: Trying to connect GPRS before potentially disconnecting WiFi.");
    if (gm->connect()) {
        DEBUG_PRINTLN(3, "NetworkFacade: GPRS connected successfully during switch attempt.");
        if (wm && wm->isConnected()) {
            DEBUG_PRINTLN(3, "NetworkFacade: Disconnecting WiFi as GPRS is now active.");
            wm->disconnect();
        }
        determineActiveInterface(); // Set GPRS as active
        return true;
    } else {
        DEBUG_PRINTLN(2, "NetworkFacade: GPRS connection failed during switch attempt. WiFi (if active) will not be disconnected.");
        // Ensure the active interface is correctly set, possibly back to WiFi if it was active and GPRS failed
        determineActiveInterface();
        return false;
    }
}

/**
* @brief Gets a pointer to the currently active underlying network interface.
* Refer to NetworkFacade.h for detailed documentation.
*/
NetworkInterface* NetworkFacade::getCurrentInterface() const {
   return _activeInterface;
}

/**
* @brief Gets the current network preference strategy.
* Refer to NetworkFacade.h for detailed documentation.
*/
NetworkFacade::NetworkPreference NetworkFacade::getPreference() const {
   return _preference;
}

/**
* @brief Sets a new network preference strategy and re-evaluates the active interface.
* Refer to NetworkFacade.h for detailed documentation.
*/
void NetworkFacade::setPreference(NetworkPreference preference) {
   DEBUG_PRINTF(3, "NetworkFacade: Setting preference to %d\n", (int)preference);
   _preference = preference;
   // After changing preference, it's crucial to re-evaluate the active interface,
   // as the desired behavior might change. A subsequent call to connect() will use this new preference.
   determineActiveInterface();
}

/**
* @brief Retrieves a pointer to the WiFiManager instance.
* Refer to NetworkFacade.h for detailed documentation.
*/
WiFiManager* NetworkFacade::getWiFiManager() const {
   return _wifiManagerRaw;
}

/**
* @brief Retrieves a pointer to the GPRSManager instance.
* Refer to NetworkFacade.h for detailed documentation.
*/
GPRSManager* NetworkFacade::getGPRSManager() const {
   return _gprsManagerRaw;
}

/**
* @brief Checks if the system is operating in a fail-safe mode via DeviceState.
* Refer to NetworkFacade.h for detailed documentation.
*/
bool NetworkFacade::isSafeModeActive() const {
   if (_deviceState) {
       return _deviceState->isInFailSafeMode;
   }
   DEBUG_PRINTLN(1, "NetworkFacade: _deviceState is null in isSafeModeActive(). Returning false as default.");
   return false; // Default to not in safe mode if device state is not available
}

// Note: The _apiResponse member variable has been removed from NetworkFacade.h
// as it was determined to be unused. Response handling is fully delegated to
// the active WiFiManager or GPRSManager instances.