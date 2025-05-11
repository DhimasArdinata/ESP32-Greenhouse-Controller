// #define TINY_GSM_DEBUG Serial // If GPRSManager specific debugging is needed, uncomment

#include "config.h" // For DEBUG_PRINTLN, pin definitions etc. (DEBUG_LEVEL)
// TinyGsmCommon.h and TinyGsmClient.h are now included via GPRSManager.h, which includes config.h first.
#include "GPRSManager.h"
#include "DeviceState.h" // Include DeviceState header
#include "LCDDisplay.h"  // For LCDDisplay class definition
#include "DeviceConfig.h" // For FW_NAME, FW_VERSION
#include <Arduino.h>  // For millis(), Serial, etc.
#include <esp_task_wdt.h> // For watchdog reset

// GPRSState enum is now defined in DeviceState.h and included.

const char* GPRSManager::gprsStateToString(GPRSState state) { // Note: Parameter type is now GPRSState from DeviceState.h
    switch (state) {
        case GPRSState::GPRS_STATE_DISABLED: return "DISABLED";
        case GPRSState::GPRS_STATE_INIT_START: return "INIT_START";
        case GPRSState::GPRS_STATE_INIT_WAIT_SERIAL: return "INIT_WAIT_SERIAL";
        case GPRSState::GPRS_STATE_INIT_RESET_MODEM: return "INIT_RESET_MODEM";
        // case GPRSState::GPRS_STATE_INIT_SET_APN: return "INIT_SET_APN"; // Removed
        case GPRSState::GPRS_STATE_INIT_ATTACH_GPRS: return "INIT_ATTACH_GPRS";
        // case GPRSState::GPRS_STATE_INIT_CONNECT_TCP: return "INIT_CONNECT_TCP"; // Removed
        case GPRSState::GPRS_STATE_OPERATIONAL: return "OPERATIONAL";
        case GPRSState::GPRS_STATE_CONNECTION_LOST: return "CONNECTION_LOST";
        case GPRSState::GPRS_STATE_RECONNECTING: return "RECONNECTING";
        case GPRSState::GPRS_STATE_ERROR_RESTART_MODEM: return "ERROR_RESTART_MODEM";
        case GPRSState::GPRS_STATE_ERROR_MODEM_FAIL: return "ERROR_MODEM_FAIL";
        default: return "UNKNOWN_STATE";
    }
}


// Constructor
GPRSManager::GPRSManager(
    TinyGsm& modem,
    const char* apn,
    const char* gprsUser,
    const char* gprsPass,
    const char* simPin,
    const char* authToken,
    DeviceState* deviceState, // Added DeviceState
    LCDDisplay* lcd)
    : _modem(modem),
      _gprsClient(modem), // Initialize GPRS client with the modem reference
      _apn(apn),
      _gprsUser(gprsUser),
      _gprsPass(gprsPass),
      _simPin(simPin),
      _authToken(authToken),
      _deviceState(deviceState), // Initialize DeviceState
      _lcd(lcd),
      _currentHttpState(GPRSHttpState::IDLE),
      _asyncOperationActive(false),
      _gprsHttpStatusCode(0),
      _gprsContentLength(0),
      _gprsChunkedEncoding(false),
      _gprsBodyBytesRead(0),
      _currentGprsState(GPRSState::GPRS_STATE_DISABLED), // Initialize GPRS FSM state
      _lastGprsStateTransitionTime(0),
      _gprsReconnectAttempt(0),
      _modemResetCount(0),
      _gprsAttachFailCount(0)
       {
   _gprsHost[0] = '\0';
   _gprsPath[0] = '\0';
  // _jsonDoc.reserve(GPRS_BODY_BUFFER_SIZE); // StaticJsonDocument pre-allocates, reserve is not needed and not a member.
}

GPRSManager::~GPRSManager() {
    if (_gprsClient.connected()) {
        _gprsClient.stop();
    }
}

void GPRSManager::setAuthToken(const char* authToken) {
    _authToken = authToken;
}


bool GPRSManager::connect() {
    // FSM will handle connection. This method now initiates the FSM if it's disabled.
    if (_currentGprsState == GPRSState::GPRS_STATE_DISABLED) {
        DEBUG_PRINTLN(3, "GPRSManager: connect() called. Starting FSM from DISABLED state.");
        transitionToState(GPRSState::GPRS_STATE_INIT_START);
    } else {
        DEBUG_PRINTF(3, "GPRSManager: connect() called. FSM already active in state: %s\n", gprsStateToString(_currentGprsState));
    }
    // The actual connection status is determined by the FSM state (_currentGprsState == GPRSState::GPRS_STATE_OPERATIONAL)
    // This function now just kicks off the process if needed.
    // Success is not immediate. Call isConnected() to check actual status.
    return true; // Indicates the process has started or is ongoing.
}

void GPRSManager::disconnect() {
    DEBUG_PRINTLN(3, "GPRSManager: Disconnecting GPRS...");
    _modem.gprsDisconnect();
    // Optionally, power down modem if not needed for a while
    // #if defined(MODEM_POWER_ON)
    //    digitalWrite(MODEM_POWER_ON, LOW); // Example
    // #endif
}

// --- GPRS FSM Implementation ---

void GPRSManager::transitionToState(GPRSState newState) {
    if (_currentGprsState != newState) {
        DEBUG_PRINTF(3, "GPRS FSM: %s -> %s\n", gprsStateToString(_currentGprsState), gprsStateToString(newState));
        _currentGprsState = newState;
        _lastGprsStateTransitionTime = millis();
        // Reset counters specific to certain state transitions if needed
        if (newState != GPRSState::GPRS_STATE_RECONNECTING) {
            _gprsReconnectAttempt = 0;
        }
        if (newState == GPRSState::GPRS_STATE_INIT_START) {
             _modemResetCount = 0; // Reset for a full new init sequence
             _gprsAttachFailCount = 0;
             // _tcpConnectFailCount = 0; // Removed
             // _apnSetRetryCount = 0; // Reset APN retry count - Removed
        }
        if (_deviceState) { // Update global device state if available
            _deviceState->currentGprsState = _currentGprsState; // Corrected member name
            _deviceState->lastGprsStateTransitionTime = _lastGprsStateTransitionTime;
        }
    }
}

unsigned long GPRSManager::getElapsedTimeInCurrentGprsState() const {
    return millis() - _lastGprsStateTransitionTime;
}

void GPRSManager::updateFSM() {
    esp_task_wdt_reset(); // Reset watchdog at the beginning of FSM update

    // Update global device state if available
    if (_deviceState) {
        _deviceState->gprsSignalQuality = getSignalQuality();
        _deviceState->isGprsConnected = isConnected();
    }

    switch (_currentGprsState) {
        case GPRSState::GPRS_STATE_DISABLED:
            // Do nothing, GPRS is not active. Call connect() to start.
            break;
        case GPRSState::GPRS_STATE_INIT_START:
            handleGprsInitStart();
            break;
        case GPRSState::GPRS_STATE_INIT_WAIT_SERIAL:
            handleGprsInitWaitSerial();
            break;
        case GPRSState::GPRS_STATE_INIT_RESET_MODEM:
            handleGprsInitResetModem();
            break;
        // case GPRSState::GPRS_STATE_INIT_SET_APN: // Removed
        //     handleGprsInitSetApn(); // Removed
        //     break;
        case GPRSState::GPRS_STATE_INIT_ATTACH_GPRS:
            handleGprsInitAttachGprs();
            break;
        // case GPRSState::GPRS_STATE_INIT_CONNECT_TCP: // Removed
        //     handleGprsInitConnectTcp(); // Removed
        //     break;
        case GPRSState::GPRS_STATE_OPERATIONAL:
            handleGprsOperational();
            break;
        case GPRSState::GPRS_STATE_CONNECTION_LOST:
            handleGprsConnectionLost();
            break;
        case GPRSState::GPRS_STATE_RECONNECTING:
            handleGprsReconnecting();
            break;
        case GPRSState::GPRS_STATE_ERROR_RESTART_MODEM:
            handleGprsErrorRestartModem();
            break;
        case GPRSState::GPRS_STATE_ERROR_MODEM_FAIL:
            handleGprsErrorModemFail();
            break;
        default:
            DEBUG_PRINTLN(1, "GPRS FSM: Reached unknown state!");
            transitionToState(GPRSState::GPRS_STATE_ERROR_MODEM_FAIL); // Should not happen
            break;
    }
}


bool GPRSManager::checkModemSerial() {
    // Simple check, can be expanded (e.g., multiple AT commands)
    return _modem.testAT(200); // Short timeout for responsiveness check
}

bool GPRSManager::performModemSoftReset() {
    DEBUG_PRINTLN(2, "GPRS FSM: Attempting modem soft reset (AT+CFUN=1,1)...");
    bool success = _modem.restart(); // TinyGSM's restart often does a soft reset
    esp_task_wdt_reset();
    if (success) {
        DEBUG_PRINTLN(3, "GPRS FSM: Modem soft reset successful.");
        delay(5000); // Wait for modem to come back online
        esp_task_wdt_reset();
        return checkModemSerial(); // Verify it's responsive
    } else {
        DEBUG_PRINTLN(1, "GPRS FSM: Modem soft reset command failed.");
        return false;
    }
}

bool GPRSManager::performModemHardReset() {
    DEBUG_PRINTLN(2, "GPRS FSM: Attempting modem hard reset (power cycle/reset pin)...");
    // Power cycling/reset sequence (ensure pins are defined in config.h)
    #if defined(MODEM_POWER_ON) // General power pin
        pinMode(MODEM_POWER_ON, OUTPUT);
        digitalWrite(MODEM_POWER_ON, LOW); delay(500);
        digitalWrite(MODEM_POWER_ON, HIGH); delay(1000);
        DEBUG_PRINTLN(3, "GPRS FSM: MODEM_POWER_ON toggled.");
    #endif

    #if defined(GSM_PWR) && GSM_PWR != -1 // Specific PWKEY pin
        pinMode(GSM_PWR, OUTPUT);
        digitalWrite(GSM_PWR, HIGH); delay(100);
        digitalWrite(GSM_PWR, LOW);  delay(1200); // PWKEY pulse to turn OFF or ON
        digitalWrite(GSM_PWR, HIGH); delay(2000); // Ensure it's high (ON state for some)
        DEBUG_PRINTLN(3, "GPRS FSM: GSM_PWR pulsed.");
    #endif

    #if defined(GSM_RST) && GSM_RST != -1 // Specific Reset pin
        pinMode(GSM_RST, OUTPUT);
        digitalWrite(GSM_RST, LOW);  delay(GPRS_MODEM_RESET_PULSE_MS); // Assert reset
        digitalWrite(GSM_RST, HIGH); delay(3000); // De-assert and wait
        DEBUG_PRINTLN(3, "GPRS FSM: GSM_RST pulsed.");
    #endif
    
    esp_task_wdt_reset();
    delay(GPRS_MODEM_POWER_CYCLE_DELAY_MS); // Generous delay for modem to boot after hard reset
    esp_task_wdt_reset();

    // After hard reset, the modem object itself might need re-initialization
    // if its internal state is not consistent with the hardware.
    // _modem.init(); // This is a more thorough re-init of TinyGSM's internal state.
    // However, _modem.restart() is often sufficient and preferred.
    bool modemOk = _modem.restart(); // Try to re-establish communication with the modem
    if (!modemOk) {
         DEBUG_PRINTLN(1, "GPRS FSM: Modem did not respond after hard reset + restart(). Trying testAT...");
         unsigned long modemStartTime = millis();
         while (millis() - modemStartTime < GPRS_MODEM_RESPONSE_TIMEOUT_MS) { // 15s timeout for AT response
             esp_task_wdt_reset();
             if (checkModemSerial()) {
                 modemOk = true;
                 break;
             }
             delay(1000);
         }
    }

    if (modemOk) {
        DEBUG_PRINTLN(3, "GPRS FSM: Modem hard reset appears successful.");
    } else {
        DEBUG_PRINTLN(1, "GPRS FSM: Modem hard reset failed (modem unresponsive).");
    }
    return modemOk;
}


void GPRSManager::handleGprsInitStart() {
    DEBUG_PRINTLN(3, "GPRS FSM: Handling GPRS_STATE_INIT_START");
    _modemResetCount = 0; // Reset for this new attempt sequence
    _gprsAttachFailCount = 0;
    // _tcpConnectFailCount = 0; // Removed
    // _apnSetRetryCount = 0; // Removed

    // If modem serial is not immediately available, go to a waiting state.
    if (!checkModemSerial()) {
        DEBUG_PRINTLN(2, "GPRS FSM: Modem serial not immediately responsive. Moving to WAIT_SERIAL.");
        transitionToState(GPRSState::GPRS_STATE_INIT_WAIT_SERIAL);
    } else {
        // If serial is fine, proceed to reset/init.
        transitionToState(GPRSState::GPRS_STATE_INIT_RESET_MODEM);
    }
}

void GPRSManager::handleGprsInitWaitSerial() {
    if (checkModemSerial()) {
        DEBUG_PRINTLN(3, "GPRS FSM: Modem serial now responsive.");
        transitionToState(GPRSState::GPRS_STATE_INIT_RESET_MODEM);
    } else if (getElapsedTimeInCurrentGprsState() > MODEM_SERIAL_WAIT_TIMEOUT_MS) { 
        DEBUG_PRINTLN(1, "GPRS FSM: Timeout waiting for modem serial. Attempting hard reset.");
        // Try a hard reset if serial doesn't come up.
        // This might lead to a cycle if hard reset also fails to bring up serial.
        // Consider adding a counter for hard resets here if this becomes an issue.
        transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM); // Go to restart, which will try hard reset
    }
    // Else, stay in this state and keep checking.
}


void GPRSManager::handleGprsInitResetModem() {
    DEBUG_PRINTLN(3, "GPRS FSM: Handling GPRS_STATE_INIT_RESET_MODEM");
    bool resetOk = false;
    if (_modemResetCount == 0) { // First try a soft reset
        resetOk = performModemSoftReset();
    } else { // Subsequent tries, use hard reset
        resetOk = performModemHardReset();
    }

    if (resetOk) {
        DEBUG_PRINTLN(3, "GPRS FSM: Modem reset successful.");
        String modemInfo = _modem.getModemInfo();
        DEBUG_PRINTF(3, "GPRS FSM: Modem Info: %s\n", modemInfo.c_str());
        _modemResetCount = 0; // Reset counter on successful reset
        
        // Unlock SIM if PIN is provided
        if (strlen(_simPin.c_str()) > 0) { // Use strlen for PROGMEM char array
            SimStatus simStatus = _modem.getSimStatus();
            esp_task_wdt_reset();
            if (simStatus == SIM_LOCKED) {
                DEBUG_PRINTLN(3, "GPRS FSM: Unlocking SIM...");
                if (!_modem.simUnlock(_simPin.c_str())) {
                    DEBUG_PRINTLN(1, "GPRS FSM: SIM Unlock Failed.");
                    transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM); // Retry cycle
                    return;
                }
                delay(1000); // Wait for unlock
                simStatus = _modem.getSimStatus(); // Check again
            }
        }

        if (_modem.getSimStatus() != SIM_READY) {
            DEBUG_PRINTF(1, "GPRS FSM: SIM not ready. Status: %d. Retrying modem reset.\n", (int)_modem.getSimStatus());
            _modemResetCount++; // Increment here before check
            if (_modemResetCount >= GPRS_MAX_MODEM_RESETS) { 
                transitionToState(GPRSState::GPRS_STATE_ERROR_MODEM_FAIL);
            } else {
                transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM); // Try reset sequence again
            }
            return;
        }
        DEBUG_PRINTLN(3, "GPRS FSM: SIM OK.");

        // Enable SSL for GPRS
        DEBUG_PRINTLN(3, "GPRS FSM: Attempting to enable SSL (AT+CIPSSL=1)...");
        _modem.sendAT(F("+CIPSSL=1"));
        if (_modem.waitResponse(10000L) != 1) { // Wait up to 10s for "OK"
            DEBUG_PRINTLN(1, "GPRS FSM: Failed to enable SSL (AT+CIPSSL=1). HTTPS might fail.");
            // Consider transitioning to an error state or retrying modem reset if SSL is critical
            // For now, log and continue. Some firmwares/modems might have it enabled by default.
        } else {
            DEBUG_PRINTLN(3, "GPRS FSM: SSL enabled successfully (AT+CIPSSL=1).");
        }
        esp_task_wdt_reset();

        // _apnSetRetryCount = 0; // Reset APN retry for the new modem state - Removed
        transitionToState(GPRSState::GPRS_STATE_INIT_ATTACH_GPRS); // Directly to ATTACH_GPRS
    } else {
        DEBUG_PRINTLN(1, "GPRS FSM: Modem reset failed.");
        _modemResetCount++;
        if (_modemResetCount >= GPRS_MAX_MODEM_RESETS) { 
            DEBUG_PRINTLN(1, "GPRS FSM: Max modem resets reached. Moving to MODEM_FAIL.");
            transitionToState(GPRSState::GPRS_STATE_ERROR_MODEM_FAIL);
        } else {
            DEBUG_PRINTF(2, "GPRS FSM: Retrying modem reset (attempt %d).\n", _modemResetCount);
            // Stay in INIT_RESET_MODEM or go back to ERROR_RESTART_MODEM to try again with delay
            // Forcing a delay via ERROR_RESTART_MODEM might be better
             transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM);
        }
    }
}

// void GPRSManager::handleGprsInitSetApn() { // Function removed
//     DEBUG_PRINTLN(3, "GPRS FSM: Handling GPRS_STATE_INIT_SET_APN");
    
//     // The call _modem.setGPRSNetworkSettings() is not a standard TinyGSM function.
//     // APN settings are typically handled by _modem.gprsConnect() or by sending specific AT commands.
//     // We will rely on gprsConnect in the GPRS_STATE_INIT_ATTACH_GPRS state to set the APN.
//     // This state can ensure the modem is ready for APN configuration if specific AT commands were to be used.
//     // For now, we assume gprsConnect is sufficient. If specific APN pre-configuration is needed
//     // before gprsConnect, AT commands like AT+CGDCONT might be used here.
//     // Let's attempt to set APN using AT+CGDCONT=1,"IP","apn"
//     // Then verify with AT+CGDCONT?

//     DEBUG_PRINTLN(3, "GPRS FSM: Ensuring APN is set (delegating to gprsConnect or specific AT commands if needed).");

//     // Simplified: Assume APN will be set by gprsConnect. If direct APN setting fails repeatedly,
//     // this logic handles retries before moving to modem restart.
//     // For this pass, we'll just transition, assuming gprsConnect in ATTACH will do the job.
//     // If gprsConnect fails due to APN, the FSM will cycle back.
//     // To make this state more robust, one could send AT+CGDCONT here.
//     // Example: String cmd = "AT+CGDCONT=1,\"IP\",\"" + _apn + "\""; _modem.sendAT(cmd);
//     // For now, let's assume it's okay to proceed to attach,
//     // as `gprsConnect` in `handleGprsInitAttachGprs` will use the APN.
//     // If `_modem.setGPRSNetworkSettings` was meant to be a placeholder for such AT commands:
    
//     // Placeholder: if (/* actual APN set command fails after retries */) {
//     // transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM);
//     // } else {
//     // transitionToState(GPRSState::GPRS_STATE_INIT_ATTACH_GPRS);
//     // }
//     // For now, just proceed as gprsConnect will try with the APN.
//     // If there was a specific AT command to *just* set APN and it failed, then retry logic would apply.
//     // Since `setGPRSNetworkSettings` is not valid, this state effectively becomes a pass-through
//     // or a point where more specific AT commands could be added.
//     // For now, we'll assume the APN is set during the gprsConnect() phase in INIT_ATTACH_GPRS
//     // and if that fails due to APN issues, the FSM will cycle.
//     // To prevent an immediate loop if APN is truly the issue and gprsConnect doesn't set it correctly
//     // without a prior specific command, we might need to add AT+CGDCONT logic here.
//     // However, TinyGSM's gprsConnect IS supposed to handle setting the APN.

//     // Let's simulate a successful "APN setting" step and move to attach.
//     // The real test of APN is when gprsConnect is called.
//     DEBUG_PRINTLN(3, "GPRS FSM: Proceeding to GPRS Attach (APN will be used by gprsConnect).");
//     // _apnSetRetryCount = 0; // Reset on conceptual success - Member removed
//     _gprsAttachFailCount = 0;
//     transitionToState(GPRSState::GPRS_STATE_INIT_ATTACH_GPRS);
    
// }


void GPRSManager::handleGprsInitAttachGprs() {
    DEBUG_PRINTLN(3, "GPRS FSM: Handling GPRS_STATE_INIT_ATTACH_GPRS");
    esp_task_wdt_reset();

    if (_modem.isNetworkConnected() && _modem.isGprsConnected()) {
         DEBUG_PRINTLN(3, "GPRS FSM: Already registered and GPRS connected.");
         _gprsAttachFailCount = 0; // Reset counter
         transitionToState(GPRSState::GPRS_STATE_OPERATIONAL); // Or INIT_CONNECT_TCP if a test ping is desired
         return;
    }
    
    DEBUG_PRINTLN(3, "GPRS FSM: Checking network registration...");
    if (!_modem.isNetworkConnected()) {
        DEBUG_PRINTLN(2, "GPRS FSM: Not registered on network. Waiting for registration...");
        // TinyGSM's waitForNetwork can take time.
        // Non-blocking check:
        SIM800RegStatus regStatus = _modem.getRegistrationStatus();
        DEBUG_PRINTF(4, "GPRS FSM: Reg Status: %d\n", static_cast<int>(regStatus));
        if (regStatus == REG_OK_HOME || regStatus == REG_OK_ROAMING) {
            DEBUG_PRINTLN(3, "GPRS FSM: Network registration OK.");
        } else {
            if (getElapsedTimeInCurrentGprsState() > GPRS_ATTACH_TIMEOUT_MS) { // GPRS_ATTACH_TIMEOUT_MS from config.h
                DEBUG_PRINTLN(1, "GPRS FSM: Network registration timeout.");
                 _gprsAttachFailCount++;
                if (_gprsAttachFailCount > GPRS_MAX_ATTACH_FAILURES) {
                    transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM); // Changed from MODEM_FAIL to allow reset cycle
                } else {
                    // Stay in this state, retry on next FSM loop or transition to a specific retry delay state.
                    // Forcing a new attempt by resetting timer
                    _lastGprsStateTransitionTime = millis(); 
                }
                return;
            }
            // Still waiting for registration, stay in this state.
            return;
        }
    }
     esp_task_wdt_reset();

    // Network is registered, now try to connect GPRS
    DEBUG_PRINTLN(3, "GPRS FSM: Attempting GPRS connect...");
    if (_modem.gprsConnect(_apn.c_str(), _gprsUser.c_str(), _gprsPass.c_str())) {
        DEBUG_PRINTLN(3, "GPRS FSM: GPRS Connected successfully.");
        _gprsAttachFailCount = 0; // Reset on success
        // _tcpConnectFailCount = 0; // Reset for next phase - Removed
        transitionToState(GPRSState::GPRS_STATE_OPERATIONAL); // Move directly to Operational
    } else {
        DEBUG_PRINTLN(1, "GPRS FSM: gprsConnect failed.");
        printModemErrorCause();
        _gprsAttachFailCount++;
        if (_gprsAttachFailCount >= GPRS_MAX_ATTACH_FAILURES) { 
            DEBUG_PRINTLN(1, "GPRS FSM: Max GPRS attach failures. Restarting modem.");
            transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM);
        } else {
            DEBUG_PRINTF(2, "GPRS FSM: GPRS attach failed, attempt %d. Will retry in this state after FSM loop delay.\n", _gprsAttachFailCount);
             _lastGprsStateTransitionTime = millis(); // Reset timer to enforce FSM loop delay before retry
        }
    }
}


// void GPRSManager::handleGprsInitConnectTcp() { // Function removed
//     DEBUG_PRINTLN(3, "GPRS FSM: Handling GPRS_STATE_INIT_CONNECT_TCP");
//     // Example: Ping a server to verify data path - Ping can be unreliable or blocked.
//     // Consider a small HTTP GET to a known lightweight endpoint as a better test if needed.
//     // For now, let's assume if gprsConnect was successful, the data path is likely okay.
//     // If _modem.ping("8.8.8.8")) { // Consider making ping target configurable or removing
//     DEBUG_PRINTLN(3, "GPRS FSM: Skipping explicit TCP ping test. Assuming data path OK after gprsConnect.");
//     // _tcpConnectFailCount = 0; // Member removed
//     transitionToState(GPRSState::GPRS_STATE_OPERATIONAL);
//     /*
//     else { // Original ping logic
//        DEBUG_PRINTLN(1, "GPRS FSM: Ping failed. TCP/IP path may have issues.");
//        _tcpConnectFailCount++;
//        if (_tcpConnectFailCount >= GPRS_MAX_TCP_CONNECT_FAILURES) {
//            DEBUG_PRINTF(1, "GPRS FSM: Max TCP connect (ping) failures (%d). Re-attaching GPRS.\n", GPRS_MAX_TCP_CONNECT_FAILURES);
//            transitionToState(GPRSState::GPRS_STATE_INIT_ATTACH_GPRS); // Go back to GPRS attach
//        } else {
//            DEBUG_PRINTF(2, "GPRS FSM: TCP connect (ping) failed, attempt %d. Will retry in this state.\n", _tcpConnectFailCount);
//            _lastGprsStateTransitionTime = millis(); // Reset timer for retry delay
//        }
//     }
//     */
// }

void GPRSManager::handleGprsOperational() {
    // DEBUG_PRINTLN(5, "GPRS FSM: Handling GPRS_STATE_OPERATIONAL"); // Too verbose
    if (getElapsedTimeInCurrentGprsState() > GPRS_CONNECTION_CHECK_INTERVAL_MS) { 
        _lastGprsStateTransitionTime = millis(); // Reset timer for this check interval
        if (!_modem.isGprsConnected()) { // isGprsConnected can be slow, consider alternatives
            DEBUG_PRINTLN(1, "GPRS FSM: GPRS connection lost (detected in OPERATIONAL by isGprsConnected).");
            transitionToState(GPRSState::GPRS_STATE_CONNECTION_LOST);
        } else if (!_modem.isNetworkConnected()) { // Also check basic network registration
             DEBUG_PRINTLN(1, "GPRS FSM: Network registration lost (detected in OPERATIONAL).");
            transitionToState(GPRSState::GPRS_STATE_CONNECTION_LOST);
        }
        else {
            // DEBUG_PRINTF(4, "GPRS FSM: Still operational. Signal: %d\n", getSignalQuality());
        }
    }
}

void GPRSManager::handleGprsConnectionLost() {
    DEBUG_PRINTLN(2, "GPRS FSM: Handling GPRS_STATE_CONNECTION_LOST. Moving to RECONNECTING.");
    _gprsClient.stop(); // Ensure any active client connection is closed
    _gprsReconnectAttempt = 0; // Reset for this new reconnection sequence
    // Don't reset modemResetCount here, that's for full init sequences
    transitionToState(GPRSState::GPRS_STATE_RECONNECTING);
}

void GPRSManager::handleGprsReconnecting() {
    DEBUG_PRINTF(3, "GPRS FSM: Handling GPRS_STATE_RECONNECTING (Attempt: %d)\n", _gprsReconnectAttempt);
    
    if (_gprsReconnectAttempt < GPRS_MAX_RECONNECT_ATTEMPTS) { 
        // Using GPRS_RECONNECT_DELAY_INITIAL_MS and GPRS_RECONNECT_DELAY_MAX_MS for backoff (simplified for now)
        unsigned long delayDuration = GPRS_RECONNECT_DELAY_INITIAL_MS; // Simplified: use initial delay for now
        if (getElapsedTimeInCurrentGprsState() > delayDuration) {
            _gprsReconnectAttempt++;
            DEBUG_PRINTF(2, "GPRS FSM: Attempting to reconnect GPRS (try %d).\n", _gprsReconnectAttempt);
             transitionToState(GPRSState::GPRS_STATE_INIT_ATTACH_GPRS); // Re-enter the attach phase
        }
    } else {
        DEBUG_PRINTLN(1, "GPRS FSM: Max GPRS reconnect attempts reached. Moving to ERROR_RESTART_MODEM.");
        transitionToState(GPRSState::GPRS_STATE_ERROR_RESTART_MODEM);
    }
}

void GPRSManager::handleGprsErrorRestartModem() {
    DEBUG_PRINTLN(1, "GPRS FSM: Handling GPRS_STATE_ERROR_RESTART_MODEM");
    _gprsClient.stop(); // Ensure client is stopped

    // _modemResetCount is managed by handleGprsInitResetModem
    // This state essentially forces a delay then a transition back to INIT_RESET_MODEM
    if (getElapsedTimeInCurrentGprsState() < GPRS_MODEM_ERROR_RESTART_DELAY_MS) { 
        return; // Wait longer before retrying reset
    }
    // Note: _modemResetCount itself is incremented in handleGprsInitResetModem upon failure there.
    // Here we are deciding to *attempt* another reset cycle.
    // The GPRS_MAX_MODEM_RESETS check in handleGprsInitResetModem will be the ultimate limiter.
    DEBUG_PRINTF(2, "GPRS FSM: Triggering modem reset sequence from error state (current reset count: %d).\n", _modemResetCount);
    transitionToState(GPRSState::GPRS_STATE_INIT_RESET_MODEM); // Go back to reset modem state
}

void GPRSManager::handleGprsErrorModemFail() {
    DEBUG_PRINTLN(1, "GPRS FSM: Handling GPRS_STATE_ERROR_MODEM_FAIL. GPRS is non-functional.");
    if (_lcd) _lcd->message(0, 0, "Modem Fail", true); // Use message() to display error

    if (getElapsedTimeInCurrentGprsState() > GPRS_MODEM_FAIL_RECOVERY_TIMEOUT_MS) {
        DEBUG_PRINTLN(1, "GPRS FSM: Modem fail recovery timeout reached. Transitioning to DISABLED to allow manual restart of FSM.");
        transitionToState(GPRSState::GPRS_STATE_DISABLED);
    }
}


// --- End GPRS FSM Implementation ---


bool GPRSManager::isConnected() const {
    return _currentGprsState == GPRSState::GPRS_STATE_OPERATIONAL;
}

String GPRSManager::getStatusString() const {
    char buffer[150]; // Increased buffer
    if (isConnected()) {
        snprintf(buffer, sizeof(buffer), "GPRS: Connected (Sig: %d)", getSignalQuality());
    } else {
        snprintf(buffer, sizeof(buffer), "GPRS: %s (Sig: %d, Rst: %d, AtchFail: %d, TCPFail: %d, APNSetFail: %d)",
                 gprsStateToString(_currentGprsState),
                 getSignalQuality(),
                 _modemResetCount,
                 _gprsAttachFailCount,
                 _tcpConnectFailCount,
                 _apnSetRetryCount);
    }
    return String(buffer);
}

int GPRSManager::getSignalQuality() const {
    // Cache signal quality? getSignalQuality() sends AT command.
    // For now, direct call.
    return _modem.getSignalQuality();
}
bool GPRSManager::isModemConnected() const {
    // Check both network registration and GPRS context.
    // isNetworkConnected() checks network registration (e.g., CREG).
    // isGprsConnected() checks if a GPRS context is active (e.g., CGATT).
    return _modem.isNetworkConnected() && _modem.isGprsConnected();
}

String GPRSManager::getIPAddress() const {
    if (isModemConnected()) { // Use the more direct check
        IPAddress ip = _modem.localIP();
        return ip.toString();
    }
    return "0.0.0.0";
}


bool GPRSManager::startAsyncHttpRequest(
    const char* url,
    const char* method,
    const char* apiType,
    const char* payload,
    std::function<bool(JsonDocument& doc)> cb,
    bool needsAuth) {

    if (_asyncOperationActive) {
        DEBUG_PRINTF(2, "GPRSManager: Async HTTP operation already active. Request '%s' ignored.\n", apiType);
        return false;
    }
    if (!isConnected()) {
        DEBUG_PRINTF(1, "GPRSManager: Not connected for HTTP. Request '%s' failed.\n", apiType);
        return false;
    }

    DEBUG_PRINTF(3, "GPRSManager: Starting Async HTTP %s for '%s' to %s\n", method, apiType, url);

    const char* p_url = url;
    const char* protocol_end = strstr(p_url, "://");
    if (!protocol_end) {
        DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Invalid URL format (no ://).\n", apiType);
        return false;
    }
    const char* host_start = protocol_end + 3;
    const char* path_start_ptr = strchr(host_start, '/');
    const char* port_colon_ptr = strchr(host_start, ':');

    // Determine the end of the host part
    const char* host_end = host_start + strlen(host_start); // Default to end of string
    if (path_start_ptr != nullptr) {
        host_end = path_start_ptr;
    }
    if (port_colon_ptr != nullptr && port_colon_ptr < host_end) { // Port colon is before path
        host_end = port_colon_ptr;
    }

    // Copy host
    size_t host_len = host_end - host_start;
    if (host_len >= GPRS_MAX_HOST_LEN) {
        DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Host too long.\n", apiType);
        return false;
    }
    strncpy(_gprsHost, host_start, host_len);
    _gprsHost[host_len] = '\0';

    // Determine and copy path
    if (path_start_ptr != nullptr) {
        // Check if path_start_ptr is before or after port_colon_ptr if port exists
        const char* actual_path_start = path_start_ptr;
        if (port_colon_ptr != nullptr && port_colon_ptr < path_start_ptr) { // e.g. http://host:port/path
             // If there's a port, the path starts after the port and the next '/'
             // No, path_start_ptr is already correct as it finds the first '/' after protocol
        }

        size_t path_len = strlen(actual_path_start);
        if (path_len >= GPRS_MAX_PATH_LEN) {
            DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Path too long.\n", apiType);
            return false;
        }
        strncpy(_gprsPath, actual_path_start, path_len);
        _gprsPath[path_len] = '\0';
    } else {
        strncpy(_gprsPath, "/", GPRS_MAX_PATH_LEN -1); // Default path
        _gprsPath[GPRS_MAX_PATH_LEN -1] = '\0';
    }

    // Determine port
    if (port_colon_ptr != nullptr && (path_start_ptr == nullptr || port_colon_ptr < path_start_ptr) ) { // Port colon is present and before path (or no path)
        _gprsPort = atoi(port_colon_ptr + 1);
    } else {
        _gprsPort = (strncmp(p_url, "https", 5) == 0) ? 443 : 80;
        if (_gprsPort == 443) {
            DEBUG_PRINTLN(3, "GPRSManager: HTTPS requested, using secure client on port 443.");
        }
    }

    _asyncUrl = url; 
    _asyncMethod = method;
    _asyncApiType = apiType;
    _asyncPayload = (payload ? payload : "");
    _asyncCb = cb;
    _asyncNeedsAuth = needsAuth;
    _asyncRequestStartTime = millis();
    _asyncOperationActive = true;
    _httpRetries = 0; // Initialize retry counter

    _gprsResponseBuffer = "";
    _gprsHttpStatusCode = 0;
    _gprsContentLength = 0;
    _gprsChunkedEncoding = false;
    _gprsBodyBytesRead = 0;
    _jsonDoc.clear();

    _currentHttpState = GPRSHttpState::CLIENT_CONNECT;
    return true;
}

void GPRSManager::updateHttpOperations() {
    if (!_asyncOperationActive) {
        return;
    }
    
    if (_currentGprsState != GPRSState::GPRS_STATE_OPERATIONAL) {
        DEBUG_PRINTF(2, "GPRSManager: HTTP op '%s' paused, GPRS not operational (State: %s).\n", _asyncApiType.c_str(), GPRSManager::gprsStateToString(_currentGprsState));
        if (_currentHttpState != GPRSHttpState::IDLE && _currentHttpState != GPRSHttpState::COMPLETE && _currentHttpState != GPRSHttpState::ERROR) {
             DEBUG_PRINTF(1, "GPRSManager: GPRS connection dropped during active HTTP op for '%s'. Aborting HTTP.\n", _asyncApiType.c_str());
             if (_gprsClient.connected()) _gprsClient.stop();
             _currentHttpState = GPRSHttpState::ERROR; 
        }
        return; 
    }

    esp_task_wdt_reset();
    unsigned long currentTime = millis();

    if (_currentHttpState != GPRSHttpState::IDLE && 
        _currentHttpState != GPRSHttpState::COMPLETE && // Don't timeout if already complete
        _currentHttpState != GPRSHttpState::ERROR &&   // Don't timeout if already in error
        currentTime - _asyncRequestStartTime > GPRS_HTTP_TOTAL_TIMEOUT_MS) { 
        DEBUG_PRINTF(1, "GPRSManager: Async HTTP operation for '%s' timed out overall.\n", _asyncApiType.c_str());
        if (_gprsClient.connected()) _gprsClient.stop();
        _currentHttpState = GPRSHttpState::ERROR;
    }

    bool bodyComplete = false; 
    bool cbOk = false;         

    switch (_currentHttpState) {
        case GPRSHttpState::IDLE:
            _asyncOperationActive = false;
            break;

        case GPRSHttpState::CLIENT_CONNECT:
             if (currentTime - _asyncRequestStartTime > GPRS_HTTP_CONNECT_TIMEOUT_MS) { 
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Timeout waiting for GPRS to be operational for client connect or client.connect() itself.\n", _asyncApiType.c_str());
                 _currentHttpState = GPRSHttpState::ERROR; // Go to error, retry logic below might catch it
                 break; 
            }
            DEBUG_PRINTF(4, "GPRSManager Async (%s): gprsClient.connect(%s:%d)\n", _asyncApiType.c_str(), _gprsHost, _gprsPort);
            if (_gprsClient.connect(_gprsHost, _gprsPort)) {
                DEBUG_PRINTF(3, "GPRSManager Async (%s): Connected to host.\n", _asyncApiType.c_str());
                _currentHttpState = GPRSHttpState::SENDING_REQUEST;
                _asyncRequestStartTime = millis(); // Reset timer for request phase
            } else {
                DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: gprsClient.connect failed.\n", _asyncApiType.c_str());
                printModemErrorCause();
                // If connect fails, it could be a transient GPRS issue.
                // Instead of immediate ERROR, try GPRS_STATE_CONNECTION_LOST to trigger GPRS FSM recovery.
                if (_currentGprsState == GPRSState::GPRS_STATE_OPERATIONAL) {
                    transitionToState(GPRSState::GPRS_STATE_CONNECTION_LOST); 
                }
                // For HTTP FSM, go to error, which will be retried if MAX_HTTP_RETRIES not met
                _currentHttpState = GPRSHttpState::ERROR; 
            }
            break;

        case GPRSHttpState::SENDING_REQUEST: {
            if (currentTime - _asyncRequestStartTime > HTTP_RESPONSE_TIMEOUT_MS) { // Timeout for sending request (includes server thinking time before headers)
                DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Timeout sending request or waiting for initial response.\n", _asyncApiType.c_str());
                if (_gprsClient.connected()) _gprsClient.stop();
                _currentHttpState = GPRSHttpState::ERROR;
                break;
            }

            char requestBuffer[GPRS_REQUEST_BUFFER_SIZE]; 
            int offset = 0;
            offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "%s %s HTTP/1.1\r\n", _asyncMethod.c_str(), _gprsPath);
            offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "Host: %s\r\n", _gprsHost);
            if (_asyncNeedsAuth && strlen(_authToken.c_str()) > 0) {
                offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "Authorization: Bearer %s\r\n", _authToken.c_str());
            }
            // Copy PROGMEM strings to RAM for snprintf
            char fwNameRAM[32]; // Adjust size as needed
            char fwVersionRAM[32]; // Adjust size as needed
            strncpy_P(fwNameRAM, FW_NAME, sizeof(fwNameRAM) - 1);
            fwNameRAM[sizeof(fwNameRAM) - 1] = '\0';
            strncpy_P(fwVersionRAM, FW_VERSION, sizeof(fwVersionRAM) - 1);
            fwVersionRAM[sizeof(fwVersionRAM) - 1] = '\0';
            offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "User-Agent: %s/%s\r\n", fwNameRAM, fwVersionRAM);
            if (strlen(_asyncPayload.c_str()) > 0) {
                offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "Content-Type: application/json\r\n");
                offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "Content-Length: %d\r\n", strlen(_asyncPayload.c_str()));
            }
            offset += snprintf(requestBuffer + offset, sizeof(requestBuffer) - offset, "Connection: close\r\n\r\n"); 
            
            if (offset >= sizeof(requestBuffer)) {
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: HTTP headers too large for request buffer.\n", _asyncApiType.c_str());
                _currentHttpState = GPRSHttpState::ERROR;
                if(_gprsClient.connected()) _gprsClient.stop();
                break;
            }
            if (strlen(_asyncPayload.c_str()) > 0) {
                if (offset + strlen(_asyncPayload.c_str()) < sizeof(requestBuffer)) {
                    memcpy(requestBuffer + offset, _asyncPayload.c_str(), strlen(_asyncPayload.c_str()));
                    offset += strlen(_asyncPayload.c_str());
                } else {
                    DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Payload too large for request buffer with headers.\n", _asyncApiType.c_str());
                    _currentHttpState = GPRSHttpState::ERROR;
                    if(_gprsClient.connected()) _gprsClient.stop();
                    break;
                }
            }
            DEBUG_PRINTF(5, "GPRS HTTP Request:\n%s\n", requestBuffer); 
            size_t sent = _gprsClient.write(reinterpret_cast<const uint8_t*>(requestBuffer), offset);
            if (sent != (size_t)offset) {
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Failed to send full request. Sent %u/%d\n", _asyncApiType.c_str(), sent, offset);
                _currentHttpState = GPRSHttpState::ERROR;
                if(_gprsClient.connected()) _gprsClient.stop();
                 if (_currentGprsState == GPRSState::GPRS_STATE_OPERATIONAL) transitionToState(GPRSState::GPRS_STATE_CONNECTION_LOST);
                break;
            }
            _gprsResponseBuffer = ""; 
            _asyncRequestStartTime = millis(); 
            _currentHttpState = GPRSHttpState::HEADERS_RECEIVING;
            DEBUG_PRINTF(3, "GPRSManager Async (%s): Request sent, awaiting headers.\n", _asyncApiType.c_str());
            break;
        }

        case GPRSHttpState::HEADERS_RECEIVING:
            if (!_gprsClient.connected() && _currentHttpState == GPRSHttpState::HEADERS_RECEIVING) {
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Client not connected while waiting for headers.\n", _asyncApiType.c_str());
                 _currentHttpState = GPRSHttpState::ERROR;
                 break; 
            }
            while (_gprsClient.available()) { 
                char c = _gprsClient.read();
                if (c == -1) continue; 
                _gprsResponseBuffer += c;
                if (_gprsResponseBuffer.length() >= GPRS_MAX_HEADER_SIZE) { 
                    DEBUG_PRINTLN(1, "GPRSManager: Max header size reached.");
                    _currentHttpState = GPRSHttpState::ERROR;
                    if (_gprsClient.connected()) _gprsClient.stop();
                    goto end_gprs_header_check_label; 
                }
                if (_gprsResponseBuffer.endsWith("\r\n\r\n")) { 
                    DEBUG_PRINTF(3, "GPRSManager Async (%s): Headers received.\n", _asyncApiType.c_str());
                    DEBUG_PRINTF(5, "GPRS HTTP Headers:\n%s\n", _gprsResponseBuffer.c_str());

                    int firstSpace = _gprsResponseBuffer.indexOf(' ');
                    int secondSpace = (firstSpace != -1) ? _gprsResponseBuffer.indexOf(' ', firstSpace + 1) : -1;
                    if (secondSpace != -1 && firstSpace != -1 && (secondSpace > firstSpace)) {
                        _gprsHttpStatusCode = _gprsResponseBuffer.substring(firstSpace + 1, secondSpace).toInt();
                        DEBUG_PRINTF(3, "GPRSManager Async (%s): Status %d\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
                    } else {
                        DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Could not parse HTTP status line: '%s'\n", _asyncApiType.c_str(), _gprsResponseBuffer.substring(0, _gprsResponseBuffer.indexOf('\r')).c_str());
                        _gprsHttpStatusCode = 0; 
                         _currentHttpState = GPRSHttpState::ERROR; 
                        if (_gprsClient.connected()) _gprsClient.stop();
                        goto end_gprs_header_check_label;
                    }

                    String tempHeaders = _gprsResponseBuffer; tempHeaders.toLowerCase();
                    int clPos = tempHeaders.indexOf("content-length:");
                    if (clPos != -1) {
                        int clValStart = clPos + strlen("content-length:");
                        int clEnd = _gprsResponseBuffer.indexOf("\r\n", clValStart);
                        if (clEnd != -1) {
                             _gprsContentLength = _gprsResponseBuffer.substring(clValStart, clEnd).toInt();
                            DEBUG_PRINTF(3, "GPRSManager Async (%s): Content-Length: %lu\n", _asyncApiType.c_str(), _gprsContentLength);
                        } else { _gprsContentLength = 0; }
                    } else { _gprsContentLength = 0; } 
                    
                    _gprsChunkedEncoding = (tempHeaders.indexOf("transfer-encoding: chunked") != -1);
                    if(_gprsChunkedEncoding) {
                        DEBUG_PRINTF(3, "GPRSManager Async (%s): Chunked transfer encoding detected.\n", _asyncApiType.c_str());
                        _gprsContentLength = 0; 
                    }

                    int bodyStartIndex = _gprsResponseBuffer.indexOf("\r\n\r\n");
                    String initialBodyChunk = (bodyStartIndex != -1) ? _gprsResponseBuffer.substring(bodyStartIndex + 4) : "";
                    _gprsResponseBuffer = initialBodyChunk; 
                    _gprsBodyBytesRead = _gprsResponseBuffer.length();

                    if (_gprsHttpStatusCode >= 200 && _gprsHttpStatusCode < 300) {
                        if (!_gprsChunkedEncoding && _gprsContentLength == 0 && _gprsBodyBytesRead == 0) { 
                            _currentHttpState = GPRSHttpState::PROCESSING_RESPONSE;
                        } else {
                            _currentHttpState = GPRSHttpState::BODY_RECEIVING;
                            _asyncRequestStartTime = millis(); 
                        }
                    } else if (_gprsHttpStatusCode == 0) { 
                        if (_gprsClient.connected()) _gprsClient.stop();
                    }
                     else { 
                        DEBUG_PRINTF(1, "GPRSManager Async (%s) HTTP Status: %d (Error/Redirect). Reading body.\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
                        _currentHttpState = GPRSHttpState::BODY_RECEIVING;
                        _asyncRequestStartTime = millis(); 
                    }
                    goto end_gprs_header_check_label; 
                }
            }
            end_gprs_header_check_label:; 

            if (_currentHttpState == GPRSHttpState::HEADERS_RECEIVING) { 
                if (currentTime - _asyncRequestStartTime > GPRS_HTTP_HEADER_TIMEOUT_MS) { 
                    DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Header receive timeout.\n", _asyncApiType.c_str());
                    _currentHttpState = GPRSHttpState::ERROR;
                    if (_gprsClient.connected()) _gprsClient.stop();
                } else if (!_gprsClient.connected()) { 
                     DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Client disconnected while waiting for headers.\n", _asyncApiType.c_str());
                     _currentHttpState = GPRSHttpState::ERROR;
                }
            }
            break;

        case GPRSHttpState::BODY_RECEIVING:
             if (!_gprsClient.connected() && !_gprsChunkedEncoding && (_gprsContentLength == 0 || _gprsBodyBytesRead < _gprsContentLength) ) {
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Client disconnected prematurely during body. Read %lu/%lu\n", _asyncApiType.c_str(), _gprsBodyBytesRead, _gprsContentLength);
                 _currentHttpState = (_gprsBodyBytesRead > 0 || (_gprsHttpStatusCode >=200 && _gprsHttpStatusCode <300 && _gprsContentLength == 0) ) ? GPRSHttpState::PROCESSING_RESPONSE : GPRSHttpState::ERROR;
                 break;
            }
            while (_gprsClient.available()) {
                if (_gprsResponseBuffer.length() < GPRS_BODY_BUFFER_SIZE - 1) {
                     char c = (char)_gprsClient.read();
                     if (c == -1) continue;
                     _gprsResponseBuffer += c;
                     _gprsBodyBytesRead++;
                } else {
                    DEBUG_PRINTF(1, "GPRSManager Async (%s) CRITICAL: Body buffer full (%d bytes)! Response truncated. JSON parsing will likely fail. Increase GPRS_BODY_BUFFER_SIZE.\n", _asyncApiType.c_str(), GPRS_BODY_BUFFER_SIZE);
                    while(_gprsClient.available()) _gprsClient.read(); // Discard remaining bytes
                    break;
                }
            }
            
            bodyComplete = false;
            if (_gprsChunkedEncoding) {
                if (_gprsResponseBuffer.indexOf("\r\n0\r\n\r\n") != -1) {
                    String unchunkedBody = "";
                    int currentPos = 0;
                    while(true) {
                        int chunkSizeHexEnd = _gprsResponseBuffer.indexOf("\r\n", currentPos);
                        if (chunkSizeHexEnd == -1) break; 
                        String chunkSizeHex = _gprsResponseBuffer.substring(currentPos, chunkSizeHexEnd);
                        long chunkSize = strtol(chunkSizeHex.c_str(), NULL, 16);
                        if (chunkSize == 0) {bodyComplete = true; break;}
                        int chunkDataStart = chunkSizeHexEnd + 2; 
                        if (chunkDataStart + chunkSize > _gprsResponseBuffer.length()) break; 
                        unchunkedBody += _gprsResponseBuffer.substring(chunkDataStart, chunkDataStart + chunkSize);
                        currentPos = chunkDataStart + chunkSize + 2; 
                        if (currentPos >= _gprsResponseBuffer.length() -1 && chunkSize !=0 ) break; // Break if not enough for next chunk size unless it's the final 0 chunk
                        else if (currentPos >= _gprsResponseBuffer.length() -1 && chunkSize ==0 ) {bodyComplete = true; break;}// Final chunk parsed
                    }
                     if (bodyComplete) _gprsResponseBuffer = unchunkedBody;
                } else if (!_gprsClient.connected() && _gprsBodyBytesRead > 0) { 
                    DEBUG_PRINTLN(2, "GPRSManager: Client disconnected during chunked transfer. Assuming complete (may be partial).");
                    bodyComplete = true; 
                }
            } else { 
                if (_gprsContentLength > 0 && _gprsBodyBytesRead >= _gprsContentLength) bodyComplete = true;
                else if (_gprsContentLength == 0 && !_gprsClient.connected()) {
                    bodyComplete = true;
                }
            }
            
            if (bodyComplete) {
                DEBUG_PRINTF(3, "GPRSManager Async (%s): Body received (bytes: %lu, CL: %lu).\n", _asyncApiType.c_str(), _gprsBodyBytesRead, _gprsContentLength);
                _currentHttpState = GPRSHttpState::PROCESSING_RESPONSE;
            } else if (currentTime - _asyncRequestStartTime > GPRS_HTTP_BODY_TIMEOUT_MS) { 
                 DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Body receive timeout. Read %lu bytes.\n", _asyncApiType.c_str(), _gprsBodyBytesRead);
                 if (_gprsBodyBytesRead > 0 && _gprsHttpStatusCode >= 200 && _gprsHttpStatusCode < 300) {
                     DEBUG_PRINTLN(2, "GPRSManager: Processing partial body from timeout.");
                    _currentHttpState = GPRSHttpState::PROCESSING_RESPONSE; 
                 } else {
                    _currentHttpState = GPRSHttpState::ERROR;
                 }
            } else if (!_gprsClient.connected() && _currentHttpState == GPRSHttpState::BODY_RECEIVING) {
                DEBUG_PRINTF(1, "GPRSManager Async (%s) Err: Client disconnected, body not complete. Read %lu.\n", _asyncApiType.c_str(), _gprsBodyBytesRead);
                if (_gprsBodyBytesRead > 0 || (_gprsHttpStatusCode >=200 && _gprsHttpStatusCode <300 && _gprsContentLength == 0) ) {
                     _currentHttpState = GPRSHttpState::PROCESSING_RESPONSE; 
                } else {
                    _currentHttpState = GPRSHttpState::ERROR;
                }
            }
            break;

        case GPRSHttpState::PROCESSING_RESPONSE: {
            DEBUG_PRINTF(4, "GPRSManager Async (%s): Processing. Status: %d\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
            DEBUG_PRINTF(5, "GPRS HTTP Body:\n%s\n", _gprsResponseBuffer.c_str());
            cbOk = false;
            if (_gprsHttpStatusCode >= 200 && _gprsHttpStatusCode < 300) { 
                if (_asyncCb) {
                    _jsonDoc.clear(); 
                    DeserializationError err = deserializeJson(_jsonDoc, _gprsResponseBuffer);
                    if (err) {
                        DEBUG_PRINTF(1, "GPRSManager Async (%s): JSON Fail: %s\n", _asyncApiType.c_str(), err.c_str());
                        DEBUG_PRINTF(4, "Failed JSON: %s\n", _gprsResponseBuffer.c_str());
                    } else {
                        cbOk = _asyncCb(_jsonDoc);
                         if (!cbOk) {
                             DEBUG_PRINTF(2, "GPRSManager Async (%s): Callback returned false.\n", _asyncApiType.c_str());
                        } else {
                            DEBUG_PRINTF(3, "GPRSManager Async (%s): Callback successful.\n", _asyncApiType.c_str());
                        }
                    }
                } else { 
                    cbOk = true;
                    DEBUG_PRINTF(3, "GPRSManager Async (%s): No CB, HTTP 2xx success.\n", _asyncApiType.c_str());
                }
            } else { 
                 DEBUG_PRINTF(1, "GPRSManager Async (%s): HTTP Error %d.\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
                 if (_asyncCb && _gprsHttpStatusCode != 0) { 
                    _jsonDoc.clear();
                    DeserializationError err = deserializeJson(_jsonDoc, _gprsResponseBuffer);
                    if (!err) {
                        DEBUG_PRINTLN(2, "GPRSManager: Calling CB for HTTP error response.");
                        _asyncCb(_jsonDoc); // Call CB but its return doesn't make cbOk true
                    }
                 }
            }
            if(_gprsClient.connected()) _gprsClient.stop();
            _currentHttpState = cbOk ? GPRSHttpState::COMPLETE : GPRSHttpState::ERROR;
            break;
        }
        case GPRSHttpState::COMPLETE:
            DEBUG_PRINTF(3, "GPRSManager Async (%s): Operation complete.\n", _asyncApiType.c_str());
            if(_gprsClient.connected()) _gprsClient.stop();
            _asyncOperationActive = false;
            _currentHttpState = GPRSHttpState::IDLE; 
            break;

        case GPRSHttpState::ERROR:
            DEBUG_PRINTF(1, "GPRSManager Async (%s): Operation failed. Status: %d. Retries: %d/%d\n", _asyncApiType.c_str(), _gprsHttpStatusCode, _httpRetries, MAX_HTTP_RETRIES);
            if(_gprsClient.connected()) _gprsClient.stop();
            
            if (isRetryableError(_gprsHttpStatusCode) && _httpRetries < MAX_HTTP_RETRIES) {
                _httpRetries++;
                DEBUG_PRINTF(2, "GPRSManager Async (%s): Retryable error (%d). Retrying in %lu ms (attempt %d).\n", _asyncApiType.c_str(), _gprsHttpStatusCode, HTTP_RETRY_DELAY_MS, _httpRetries);
                _asyncRequestStartTime = millis() + HTTP_RETRY_DELAY_MS; // Set start time for delay
                _currentHttpState = GPRSHttpState::RETRY_WAIT; // Go to a wait state before retry
            } else {
                if (!isRetryableError(_gprsHttpStatusCode)) {
                    DEBUG_PRINTF(1, "GPRSManager Async (%s): Non-retryable HTTP error %d. Final failure.\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
                } else { // Max retries reached for a retryable error
                    DEBUG_PRINTF(1, "GPRSManager Async (%s): Max HTTP retries reached for error %d. Final failure.\n", _asyncApiType.c_str(), _gprsHttpStatusCode);
                }
                _asyncOperationActive = false;
                _currentHttpState = GPRSHttpState::IDLE;
            }
            break;
        case GPRSHttpState::RETRY_WAIT:
            if (millis() >= _asyncRequestStartTime) { // Check if delay has passed
                DEBUG_PRINTF(2, "GPRSManager Async (%s): Retry delay complete. Attempting retry %d.\n", _asyncApiType.c_str(), _httpRetries);
                // Reset relevant HTTP state variables before retrying
                _gprsResponseBuffer = "";
                _gprsHttpStatusCode = 0;
                _gprsContentLength = 0;
                _gprsChunkedEncoding = false;
                _gprsBodyBytesRead = 0;
                _jsonDoc.clear();
                _asyncRequestStartTime = millis(); // Reset start time for the new attempt
                _currentHttpState = GPRSHttpState::CLIENT_CONNECT; // Start retry from client connect
            }
            // Else, continue waiting
            break;
        default: 
            DEBUG_PRINTF(1, "GPRSManager Async (%s): Unhandled GPRSHttpState %d\n", _asyncApiType.c_str(), (int)_currentHttpState);
            if(_gprsClient.connected()) _gprsClient.stop();
            _currentHttpState = GPRSHttpState::ERROR; 
            _asyncOperationActive = false; 
            break;
    }
}

void GPRSManager::printModemErrorCause() {
    #if DEBUG_LEVEL >= 1 
    // AT+CEER might not be universally supported or could interfere.
    // String errorCause;
    // _modem.sendAT(F("+CEER"));
    // if (_modem.waitResponse(1000L, errorCause) == 1 && errorCause.length() > 0) {
    //    DEBUG_PRINTF(1, "Modem CEER: %s\n", errorCause.c_str());
    // }
    #endif
}
bool GPRSManager::isRetryableError(int httpStatusCode) {
    // For GPRS, TinyGSM often returns 0 or negative for client-side/connection issues
    // before an HTTP status code is even obtained.
    // A positive HTTP status code means the server responded.
    if (httpStatusCode <= 0) { // Connection failed, timeout, etc.
        return true;
    }
    // Specific 4xx errors that might be retryable
    if (httpStatusCode == 408) { // Request Timeout
        return true;
    }
    if (httpStatusCode == 429) { // Too Many Requests
        return true;
    }
    // All 5xx server-side errors are generally considered retryable
    if (httpStatusCode >= 500 && httpStatusCode <= 599) {
        return true;
    }
    // Most other 4xx errors (400, 401, 403, 404, etc.) are typically client errors
    // and shouldn't be retried with the same request.
    return false;
}