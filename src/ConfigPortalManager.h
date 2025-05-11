/**
 * @file ConfigPortalManager.h
 * @brief Defines the `ConfigPortalManager` class for providing a web-based configuration interface
 *        when the device cannot connect to its primary network or during initial setup.
 *
 * This file declares the `ConfigPortalManager` class, which is responsible for:
 * 1.  Creating a Wi-Fi Access Point (AP): It sets up a temporary Wi-Fi network with
 *     credentials typically defined in `config.h` (e.g., `AP_SSID` and `AP_PASSWORD`).
 *     Users can connect their devices (smartphones, laptops) directly to this AP.
 * 2.  Running a Web Server: It hosts a web page that allows users to view and modify
 *     the device's configuration settings.
 * 3.  Implementing Captive Portal Functionality: Using a `DNSServer`, it redirects all
 *     DNS queries from connected clients to the device's IP address. This usually prompts
 *     the client's operating system to automatically open the configuration web page in a browser
 *     when the user attempts to access any website.
 *
 * The manager interacts closely with:
 * - `DeviceConfig`: This object is used to load the current device settings for display on the
 *   web page and to save any new settings submitted by the user through the portal.
 * - `LCDDisplay`: Status messages related to the portal's operation (e.g., "Config Portal Active",
 *   "Connect to AP: [SSID]", IP address, timeout countdown) are shown on an LCD.
 * - `NetworkFacade` (optional): If provided, this interface is notified when network-related
 *   configurations (like Wi-Fi credentials for the main network) are changed via the portal.
 *   This allows the `NetworkFacade` to attempt reconnection using the new settings.
 *
 * The HTML content for the configuration page (`CONFIG_PAGE`) is typically stored in PROGMEM
 * (flash memory) to conserve RAM, which is crucial for resource-constrained microcontrollers.
 * The portal usually has a timeout period (`PORTAL_TIMEOUT`, defined in `config.h`). If no
 * configuration is successfully saved within this period, the portal automatically stops to
 * allow the device to resume normal operation or attempt other connection methods.
 */
#ifndef CONFIG_PORTAL_MANAGER_H
#define CONFIG_PORTAL_MANAGER_H

#include <WebServer.h>      // For `WebServer` class to handle HTTP requests.
#include <DNSServer.h>      // For `DNSServer` class for captive portal functionality.
#include <functional>       // For `std::function`, used by `WebServer` for request handlers (e.g., template processing).
#include "DeviceConfig.h"   // Interface for loading and saving device configuration data.
#include "LCDDisplay.h"     // Interface for displaying status messages and portal information.
#include "NetworkFacade.h"  // Abstract interface for network operations, used to signal config changes.
#include "config.h"         // For portal-specific configurations: `AP_SSID`, `AP_PASSWORD`, `PORTAL_TIMEOUT`,
                            // `DEBUG_MODE_PORTAL`, etc.

// Forward declaration for NetworkFacade is not strictly necessary here as the full header is included,
// but it's good practice if only pointer/reference types were used from it in this header.

/**
 * @class ConfigPortalManager
 * @brief Manages a Wi-Fi Access Point (AP) and an embedded Web Server to provide a
 *        user-friendly interface for device configuration.
 *
 * This class encapsulates all the logic required to run a "captive portal".
 * When `startPortal()` is invoked, it performs the following sequence:
 * 1.  Temporarily disconnects from any existing Wi-Fi client connection.
 * 2.  Activates a Wi-Fi AP using predefined credentials (e.g., `AP_SSID` and `AP_PASSWORD` from `config.h`).
 * 3.  Starts a DNS server. This server is configured to respond to all DNS queries with the
 *     IP address of the device's AP, effectively redirecting any web request to the portal.
 * 4.  Starts a `WebServer` instance, which serves the HTML configuration page. It sets up
 *     handlers for various HTTP routes:
 *     - `/`: Serves the main configuration page.
 *     - `/save`: Handles form submissions to save the new configuration.
 *     - `/factoryreset`: Handles requests to reset the device to its default settings.
 *     - A "not found" handler that also plays a role in captive portal detection.
 *
 * The portal remains active, processing DNS and HTTP requests, until one of the following occurs:
 * - The user successfully submits new configuration settings via the `/save` endpoint.
 * - A factory reset is triggered via the `/factoryreset` endpoint.
 * - The `PORTAL_TIMEOUT` (a duration specified in `config.h`) elapses without any
 *   successful configuration submission.
 *
 * The `startPortal()` method is blocking and will only return after one of these conditions is met.
 */
class ConfigPortalManager {
public:
    /**
     * @brief Constructs a `ConfigPortalManager` instance.
     * Initializes the manager with references to necessary peripheral and configuration objects.
     *
     * @param config A reference to the `DeviceConfig` object. This is essential for:
     *               - Loading current configuration values to pre-fill the fields on the web portal.
     *               - Saving new configuration values submitted by the user via the portal.
     * @param lcd A reference to the `LCDDisplay` object. This is used to display operational
     *            status messages to the user, such as the AP name, the device's IP address
     *            in AP mode, instructions to connect, and a countdown for the portal timeout.
     * @param networkFacade A pointer to an object implementing the `NetworkFacade` interface.
     *                      This is used to notify the main network management system when
     *                      network-critical configurations (like primary WiFi SSID/password)
     *                      have been updated through the portal. This allows the `NetworkFacade`
     *                      to, for example, attempt to reconnect to the primary WiFi network
     *                      with the new credentials. This can be `nullptr` if no such
     *                      notification mechanism is required or available.
     */
    ConfigPortalManager(DeviceConfig& config, LCDDisplay& lcd, NetworkFacade* networkFacade);

    /**
     * @brief Destructor for `ConfigPortalManager`.
     * Ensures that the web server (`_server`) and DNS server (`_dnsServer`) are properly
     * stopped to release resources and network ports. This typically involves calling
     * `_server.stop()` and `_dnsServer.stop()`.
     */
    ~ConfigPortalManager();

    /**
     * @brief Starts the configuration portal in Access Point (AP) mode.
     *
     * This is a **blocking** method. It takes over the device's main loop functionality
     * until the portal session ends. The sequence of operations is:
     * 1.  Displays "Portal Starting" messages on the LCD.
     * 2.  Sets up the ESP32's Wi-Fi in AP mode using `WiFi.softAP()`. The SSID and password
     *     for this AP are typically `AP_SSID` and `AP_PASSWORD` from `config.h`.
     * 3.  Starts the DNS server (`_dnsServer.start()`) to capture all DNS requests from clients
     *     connected to the AP and redirect them to the device's own IP address (usually 192.168.4.1).
     *     This is key to the captive portal mechanism.
     * 4.  Configures and starts the `WebServer` (`_server.begin()`) by setting up handlers for:
     *     - Root path (`/`) using `handleRoot()` to serve the main configuration page.
     *     - Save path (`/save`) using `handleSave()` to process submitted configuration data.
     *     - Factory reset path (`/factoryreset`) using `handleFactoryReset()`.
     *     - A "not found" handler (`handleNotFound()`) which also aids captive portal detection.
     * 5.  Displays "Portal Active", the AP SSID, and IP address on the LCD.
     *
     * The method then enters a loop, repeatedly calling `_dnsServer.processNextRequest()` and
     * `_server.handleClient()`. This loop continues, serving web pages and processing DNS, until:
     *   a. The user successfully submits the configuration via the `/save` endpoint.
     *      In this case, `_configSaved` flag (internal state) is set to `true`.
     *   b. The `PORTAL_TIMEOUT` (a duration defined in `config.h`, e.g., 180 seconds) elapses.
     *      A countdown is typically shown on the LCD.
     *
     * Upon exiting the loop (due to save or timeout):
     * - The DNS server and WebServer are stopped.
     * - The Wi-Fi AP mode is turned off.
     * - LCD messages indicate portal closure.
     *
     * @return `true` if the configuration was successfully saved by the user through the web interface
     *         before the timeout.
     * @return `false` if the portal timed out before configuration was saved, or if an error
     *         occurred during the setup phase (e.g., AP failed to start).
     */
    bool startPortal();

    // The `handleClient()` methods for WebServer and DNSServer are called internally
    // within the blocking loop of `startPortal()`. If `startPortal()` were designed to be
    // non-blocking (requiring repeated calls from the main application `loop()`), then a
    // public `update()` or `handleRequests()` method would be necessary here.

private:
    DeviceConfig& _deviceConfig;    ///< Reference to the global `DeviceConfig` object for loading/saving settings.
    LCDDisplay& _lcd;               ///< Reference to the `LCDDisplay` object for providing visual feedback to the user.
    NetworkFacade* _networkFacade;  ///< Optional pointer to a `NetworkFacade` implementation. If not null, its `notifyConfigChanged()` (or similar) method is called after new network settings are saved, allowing the facade to re-initialize the primary network connection.

    WebServer _server;              ///< Instance of `WebServer` (from ESP8266WebServer or ESP32WebServer library) listening on port 80 for HTTP requests.
    DNSServer _dnsServer;           ///< Instance of `DNSServer` used to implement the captive portal. It listens on UDP port 53.

    bool _configSaved; ///< Internal flag set to true when the user successfully saves configuration via the portal. This determines the return value of startPortal().

    /**
     * @brief Handles HTTP GET requests to the root ("/") path of the web server.
     *
     * This method is registered with the `_server` to respond to requests for the main page
     * of the configuration portal. It serves the HTML content stored in `CONFIG_PAGE` (a PROGMEM string).
     * The `WebServer::sendContent_P()` method is often used here.
     * Before sending the page, it typically uses the `processor` callback function
     * (via `_server.send_P(200, "text/html", CONFIG_PAGE, processor)`) to dynamically
     * replace placeholders in the HTML template with current values from `_deviceConfig`
     * (e.g., current WiFi SSID, API key, operational parameters).
     */
    void handleRoot();

    /**
     * @brief Handles HTTP POST requests to the "/save" path, which is typically the target
     *        action of the configuration form submission.
     *
     * This method performs the following actions:
     * 1.  Retrieves submitted form data using `_server.arg("paramName")` for each expected parameter
     *     (e.g., "wifi_ssid", "wifi_password", "api_key", threshold values).
     * 2.  (Optional but recommended) Validates the received data (e.g., check for empty strings if
     *     a field is required, check numerical ranges).
     * 3.  Updates the corresponding fields in the `_deviceConfig` object with the new values.
     * 4.  Calls `_deviceConfig.save()` to persist these changes (e.g., to EEPROM, SPIFFS, or NVS).
     * 5.  Sends an HTTP response to the client, usually a success message page (e.g., "Configuration Saved! Rebooting...")
     *     or a redirect to the root page with a success query parameter.
     * 6.  Sets the internal `_configSaved` flag to `true`, indicating to `startPortal()` that it should
     *     exit successfully.
     * 7.  If `_networkFacade` is not `nullptr` and network-related settings were changed,
     *     it calls a method on `_networkFacade` (e.g., `_networkFacade->credentialsUpdated()`)
     *     to inform it that it might need to reconnect with new credentials.
     * 8.  Optionally, it might trigger an automatic device restart (`ESP.restart()`) after a short delay
     *     to ensure all new settings are applied cleanly, especially if fundamental settings like
     *     WiFi credentials were changed.
     */
    void handleSave();

    /**
     * @brief Handles HTTP GET requests to the "/factoryreset" path.
     *
     * This method is invoked when the user clicks a "Factory Reset" button on the portal.
     * Its actions are:
     * 1.  Calls `_deviceConfig.resetToDefaults()` to revert all configurable settings stored in
     *     the `_deviceConfig` object back to their original, hardcoded factory default values.
     * 2.  Calls `_deviceConfig.save()` to persist these default settings, overwriting any
     *     custom user configuration.
     * 3.  Sends an HTTP response to the client, typically a confirmation page indicating that
     *     the factory reset was successful and the device will restart.
     * 4.  Triggers a device restart (`ESP.restart()`) to apply the factory settings from a
     *     clean state. The restart usually happens after a short delay to allow the HTTP
     *     response to be sent.
     * The `_configSaved` flag is typically *not* set by this handler, as a factory reset might
     * imply the user did not save a *new* custom configuration. However, the portal loop will exit.
     */
    void handleFactoryReset();

    /**
     * @brief Handles all HTTP requests that do not match other explicitly defined routes.
     * This effectively serves as a 404 "Not Found" handler.
     *
     * However, its primary role in this context is to support **captive portal functionality**.
     * When a device connects to the AP and the user tries to open any website, the operating
     * system often sends a request to a known URL (e.g., "http://connectivitycheck.gstatic.com/generate_204"
     * for Android, or similar URLs for iOS/Windows) to check for internet connectivity.
     *
     * This `handleNotFound` method, in conjunction with the DNS server redirecting all domains
     * to the ESP32's IP, will catch these requests. It then calls `handleCaptivePortal()`.
     * If `handleCaptivePortal()` identifies the request as a known captive portal check URL
     * (by inspecting `_server.hostHeader()`), it sends an HTTP redirect (302 Found) to the
     * root of the portal (`/`). This redirect signals to the OS that it's behind a captive portal,
     * prompting it to open the portal page for the user.
     *
     * If `handleCaptivePortal()` returns `false` (meaning it wasn't a recognized captive portal check),
     * then a standard "404 Not Found" message is sent to the client.
     */
    void handleNotFound();
    
    /**
     * @brief Helper function specifically designed to detect and handle captive portal detection requests.
     *
     * This function is called by `handleNotFound()`. It checks if the `Host` header of the incoming
     * HTTP request matches common domains used by operating systems for captive portal detection
     * (e.g., "connectivitycheck.gstatic.com", "captive.apple.com").
     *
     * @return `true` if the request's `Host` header matches a known captive portal check domain AND
     *         an HTTP 302 redirect to the portal's root page (e.g., "http://192.168.4.1/") was
     *         successfully sent to the client. This signals the client OS to display the portal.
     * @return `false` if the `Host` header does not match any known captive portal check domains,
     *         in which case `handleNotFound()` will typically proceed to send a standard 404 error.
     */
    bool handleCaptivePortal();

    /**
     * @brief Template processor callback function for the `WebServer`.
     *
     * This function is registered with `_server.send_P()` (or similar methods that serve
     * content from PROGMEM with placeholders). When the `WebServer` serves the HTML template
     * (`CONFIG_PAGE`), it encounters placeholders in the format `%PLACEHOLDER_NAME%`.
     * For each such placeholder, this `processor` function is called with the `var`
     * argument being the `PLACEHOLDER_NAME` string (without the `%` symbols).
     *
     * The function then dynamically generates and returns the string value that should
     * replace that specific placeholder. This is typically used to inject current
     * configuration values from `_deviceConfig` (e.g., current Wi-Fi SSID, API key,
     * temperature thresholds) or other dynamic information (e.g., device ID, firmware version)
     * into the HTML page just before it's sent to the client.
     *
     * Example placeholders it might handle:
     * - `WIFI_SSID`: Returns `String(_deviceConfig.ssid)`.
     * - `API_KEY`: Returns `String(_deviceConfig.api_token)`.
     * - `DEVICE_ID`: Returns a unique device identifier.
     * - `FW_VERSION`: Returns the current firmware version from `config.h`.
     *
     * @param var The name of the placeholder string (e.g., "WIFI_SSID", "API_KEY") found in the HTML template.
     * @return A `String` containing the value to substitute for the placeholder. If the `var`
     *         is not a recognized placeholder, an empty string or a default "N/A" string is returned.
     */
    String processor(const String& var);

    /**
     * @brief Stores the HTML content for the device configuration web page.
     *
     * This `static const char` array is typically defined in the corresponding `.cpp` file
     * (e.g., `ConfigPortalManager.cpp`) and is marked with the `PROGMEM` attribute.
     * This attribute ensures that the large string containing the HTML, CSS, and JavaScript
     * for the portal page is stored in the microcontroller's flash memory rather than SRAM.
     * This is critical for conserving precious SRAM on memory-constrained devices like
     * the ESP8266 or ESP32.
     *
     * The HTML page usually includes:
     * - A form with input fields for all configurable parameters (WiFi credentials, API keys, server addresses, operational thresholds, etc.).
     * - A "Save" button to submit the form to the `/save` endpoint.
     * - Optionally, a "Factory Reset" button linking to the `/factoryreset` endpoint.
     * - JavaScript for client-side validation or dynamic interactions (though often kept minimal).
     * - Placeholders like `%WIFI_SSID%` that are replaced by the `processor` function.
     */
    static const char CONFIG_PAGE[] PROGMEM;
};

#endif // CONFIG_PORTAL_MANAGER_H