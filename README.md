# Project2R_GH1_exp - ESP32 Greenhouse Controller

This project is an ESP32-based controller for a greenhouse environment, designed to monitor sensors and manage relays. It supports WiFi and GPRS connectivity, data logging to an SD card, and a web configuration portal.

## Getting Started

### Prerequisites

1. **Hardware**:
    * ESP32 Development Board (e.g., ESP32-WROOM-32)
    * Sensors (Temperature, Humidity, Light, Soil Moisture, Water Level, pH, EC, etc. as per your setup)
    * Relays for controlling actuators (Fans, Pumps, Lights, etc.)
    * SD Card module (if SD logging is used)
    * SIM Module (e.g., SIM800L, A7670) if GPRS connectivity is required
    * LCD Display (e.g., I2C 16x2 or 20x4)
2. **Software**:
    * [PlatformIO IDE](https://platformio.org/platformio-ide) (recommended for building and uploading firmware)
    * Git (for cloning the repository)

### Configuration

Before compiling and uploading the firmware, you **MUST** configure critical settings. The recommended way is to use the **Web Configuration Portal** after the first boot. However, you can also set default values directly in the code.

#### 1. Web Configuration Portal (Recommended)

* On the first boot, or if WiFi credentials are not yet set, the device will start in Access Point (AP) mode.
* Connect to the Wi-Fi network named "**GH_Config_Portal_AP**" (password: `123456789`).
* Once connected, open a web browser and navigate to `http://192.168.4.1`.
* Use the portal to configure:
  * WiFi SSIDs and Passwords (for Greenhouse 1 and Greenhouse 2)
  * GPRS APN, User, and Password (if applicable)
  * SIM Card PIN (if applicable)
  * API Base URLs for data submission and status retrieval
  * API Authentication Token
  * World Time API URL (for RTC synchronization)
  * Device ID (Greenhouse ID)
  * Other device-specific settings.
* Settings saved via the portal are stored in Non-Volatile Storage (NVS) on the ESP32 and will persist across reboots.

#### 2. Manual Configuration (via `src/config.h`)

If you need to set initial default values that will be used before the web portal configuration, or if you prefer to hardcode them (not recommended for sensitive data if the code is public), you can edit the placeholders in [`src/config.h`](src/config.h:0).

Open [`src/config.h`](src/config.h:0) and look for the following sections:

* **WiFi Credentials**:
  * `DEFAULT_WIFI_SSID_GH1`: Replace `"YOUR_WIFI_SSID_GH1"`
  * `DEFAULT_WIFI_PWD_GH1`: Replace `"YOUR_WIFI_PASSWORD_GH1"`
  * `DEFAULT_WIFI_SSID_GH2`: Replace `"YOUR_WIFI_SSID_GH2"`
  * `DEFAULT_WIFI_PWD_GH2`: Replace `"YOUR_WIFI_PASSWORD_GH2"`
* **GPRS Credentials**:
  * `GPRS_APN`: Replace `"YOUR_GPRS_APN"` with your SIM provider's APN.
  * `GPRS_USER`: Replace `"YOUR_GPRS_USER"` (often blank).
  * `GPRS_PASSWORD`: Replace `"YOUR_GPRS_PASSWORD"` (often blank).
  * `SIM_PIN`: Replace `"YOUR_SIM_PIN"` (leave blank if no SIM PIN).
* **API URLs**:
  * `DEFAULT_API_THD_BASE_URL`: Replace `"YOUR_API_THD_BASE_URL_GH1"` (and `_GH2` if different)
  * `DEFAULT_API_AVG_SENSOR_BASE_URL`: Replace `"YOUR_API_AVG_SENSOR_BASE_URL_GH1"` (and `_GH2` if different)
  * `DEFAULT_API_STATUS_GET_BASE_URL`: Replace `"YOUR_API_STATUS_GET_BASE_URL_GH1"` (and `_GH2` if different)
  * `DEFAULT_API_STATUS_POST_BASE_URL`: Replace `"YOUR_API_STATUS_POST_BASE_URL_GH1"` (and `_GH2` if different)
* **World Time API URL**:
  * `WORLDTIME_URL`: Replace `"YOUR_WORLDTIME_API_URL"` (e.g., `"http://worldtimeapi.org/api/timezone/Asia/Jakarta"`)
* **API Authentication Token**:
  * `AUTH`: Replace `"YOUR_API_TOKEN"`

**IMPORTANT SECURITY NOTE:**
If you are making this repository public (e.g., on GitHub), ensure that you **DO NOT COMMIT ACTUAL CREDENTIALS OR SENSITIVE API KEYS** to [`src/config.h`](src/config.h:0). Use the placeholder values. Rely on the Web Configuration Portal to set these values on the device.

### Building and Uploading

1. Open the project in PlatformIO IDE.
2. Configure your `platformio.ini` if necessary (e.g., `upload_port`).
3. Build the project (PlatformIO: Build).
4. Upload the firmware to your ESP32 (PlatformIO: Upload).
5. Open the Serial Monitor (PlatformIO: Serial Monitor) to observe logs.

## Project Structure

* `.gitignore`: Specifies intentionally untracked files that Git should ignore.
* `platformio.ini`: PlatformIO project configuration file.
* `src/`: Contains the main source code for the firmware.
  * `Project2R_GH1_exp.ino`: Main application file (setup and loop).
  * `config.h`: Main configuration header, including default credentials (placeholders), pin definitions, and operational parameters. **Modify placeholders here if not using the web portal for initial setup.**
  * `DeviceConfig.h/.cpp`: Manages loading and saving device configuration from/to NVS.
  * `ConfigPortalManager.h/.cpp`: Manages the WiFiManager-based web configuration portal.
  * `WiFiManager.h/.cpp`: Handles WiFi connectivity and AP mode for configuration.
  * `GPRSManager.h/.cpp`: Manages GPRS connectivity.
  * `NetworkFacade.h/.cpp`: Provides a unified interface for network operations (WiFi/GPRS).
  * `NetworkInterface.h`: Abstract interface for network modules.
  * `SensorDataManager.h/.cpp`: Reads data from various sensors.
  * `RelayController.h/.cpp`: Controls relays based on sensor data or commands.
  * `LCDDisplay.h/.cpp`: Manages the LCD screen output.
  * `RTCManager.h/.cpp`: Manages the Real-Time Clock, including NTP synchronization.
  * `SDCardLogger.h/.cpp`: Logs data to an SD card.
  * `DeviceState.h`: Defines states and data structures for the device.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

## License

This project is licensed under the MIT License - see the LICENSE.md file (if available) for details.
(You might want to add a LICENSE.md file if you haven't already).
