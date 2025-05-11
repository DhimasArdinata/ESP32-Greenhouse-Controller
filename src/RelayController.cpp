#include "RelayController.h"
#include "config.h"     // For DEBUG_PRINTLN, DEBUG_PRINTF, RELAY_CHx pins
#include <Arduino.h> // For pinMode, digitalWrite, millis, String

// printDebugStatus was a global function from the .ino file, now removed.
// Using DEBUG_PRINTLN/F and LCD messages directly.

RelayController::RelayController(LCDDisplay& d) : _lcd(d) {
    for (int i = 0; i < 4; ++i) {
        _states[i] = false; // Initialize all relays to OFF
    }
    for (int i = 0; i < 3; ++i) {
        _manualOverrideActive[i] = false;
        _manualOverrideTargetState[i] = false;
        _manualOverrideEndTime[i] = 0;
    }
}

void RelayController::begin() {
    _pins[0] = RELAY_CH1;
    _pins[1] = RELAY_CH2;
    _pins[2] = RELAY_CH3;
    _pins[3] = RELAY_CH4;

    DEBUG_PRINTLN(3, "RelayController: Initializing relays...");


    for (int i = 0; i < 4; ++i) {
        pinMode(_pins[i], OUTPUT);
        digitalWrite(_pins[i], HIGH); // Relays are LOW state, so HIGH is OFF
        _states[i] = false;
    }
    DEBUG_PRINTLN(3, "RelayController: Relays OK (All OFF)");
}

bool RelayController::updateSingleRelayState(int relayIndex, float humidityValue, float humidityMin, float humidityMax, 
                                             float tempValue, float tempMin, float tempMax) {
    if (relayIndex < 0 || relayIndex > 2) return false; // Only control first 3 relays (0, 1, 2)

    bool oldState = _states[relayIndex];
    bool targetState = oldState; // Assume no change initially

    if (_manualOverrideActive[relayIndex] && millis() < _manualOverrideEndTime[relayIndex]) {
        targetState = _manualOverrideTargetState[relayIndex]; // Manual override is active
    } else {
        if (_manualOverrideActive[relayIndex]) { // Manual override just expired
            _manualOverrideActive[relayIndex] = false;
            DEBUG_PRINTF(3, "RelayController: R%d manual override expired.\n", relayIndex + 1);
        }

        // Automatic control logic
        switch (relayIndex) {
            case 0: // Exhaust (RELAY_CH1) - Humidity based
            case 1: // Dehumidifier (RELAY_CH2) - Humidity based
                if (humidityValue < 0.0) { // Invalid humidity reading (e.g., -1.0 from sensor error)
                    targetState = false;    // Default to OFF if sensor reading is bad
                    DEBUG_PRINTF(2, "RelayController Warn: R%d invalid humidity (%.1f), defaulting to OFF.\n", relayIndex + 1, humidityValue);
                } else {
                    // Turn ON if humidity is too low OR too high (outside the comfortable range)
                    targetState = (humidityValue < humidityMin || humidityValue > humidityMax);
                }
                break;
            case 2: // Blower (RELAY_CH3) - Temperature based
                if (tempValue < -40.0) { // Invalid temperature reading (e.g., -99.9 from sensor error)
                    targetState = false;   // Default to OFF if sensor reading is bad
                    DEBUG_PRINTF(2, "RelayController Warn: R%d invalid temperature (%.1f), defaulting to OFF.\n", relayIndex + 1, tempValue);
                } else {
                    // Turn ON if temperature is too low OR too high (outside the comfortable range)
                    targetState = (tempValue < tempMin || tempValue > tempMax);
                }
                break;
            // Relay 4 (index 3) is not controlled by this logic, handled by ensureRelay4Off()
        }
    }

    if (_states[relayIndex] != targetState) { // If state needs to change
        _states[relayIndex] = targetState;
        digitalWrite(_pins[relayIndex], _states[relayIndex] ? LOW : HIGH); // LOW to activate relay (common for low-state relays)
        DEBUG_PRINTF(3, "RelayController: R%d -> %s %s\n", 
            relayIndex + 1, 
            _states[relayIndex] ? "ON" : "OFF", 
            (_manualOverrideActive[relayIndex] && millis() < _manualOverrideEndTime[relayIndex]) ? "(MAN)" : "(AUTO)");
        return true; // State changed
    }
    return false; // No state change
}

void RelayController::ensureRelay4Off() {
    if (_states[3]) { // If Relay 4 (index 3) is ON
        digitalWrite(_pins[3], HIGH); // Turn it OFF (HIGH for low-state relay)
        _states[3] = false;
        DEBUG_PRINTLN(3, "RelayController: R4 (Unused) forced OFF.");
    }
}

void RelayController::setManualOverride(int relayIndex, bool desiredState, unsigned long durationMs) {
    if (relayIndex < 0 || relayIndex > 2) return;
    _manualOverrideActive[relayIndex] = true;
    _manualOverrideTargetState[relayIndex] = desiredState;
    _manualOverrideEndTime[relayIndex] = millis() + durationMs;
    DEBUG_PRINTF(2, "RelayController: Manual R%d -> %s for %lu s\n", 
        relayIndex + 1, 
        desiredState ? "ON" : "OFF", 
        durationMs / 1000);
    // Immediately apply the override state
    updateSingleRelayState(relayIndex, 0,0,0,0,0,0); // Call update to reflect change, sensor values don't matter here
}

void RelayController::forceSafeState() {
    DEBUG_PRINTLN(1, "RelayController: Forcing safe state (All relays OFF).");
    for (int i = 0; i < 3; ++i) { // For relays 1, 2, 3
        _manualOverrideActive[i] = false; // Cancel any manual override
        if (_states[i]) { // If relay is ON
            digitalWrite(_pins[i], HIGH); // Turn it OFF
            _states[i] = false;
        }
    }
    ensureRelay4Off(); // Ensure relay 4 is also off
}

bool RelayController::getR1() const { return _states[0]; }
bool RelayController::getR2() const { return _states[1]; }
bool RelayController::getR3() const { return _states[2]; }
bool RelayController::getR4() const { return _states[3]; } // Relay 4 is generally kept OFF
void RelayController::setState(int relayIndex, bool state) {
    if (relayIndex < 0 || relayIndex >= 4) { // NUM_RELAYS is effectively 4 (indices 0-3)
        DEBUG_PRINTF(1, "RelayController Error: setState called with invalid index %d\n", relayIndex);
        return;
    }
    // Cancel manual override if this relay is being set directly,
    // as direct setState implies overriding any previous manual state for that relay.
    // This applies only to relays 0, 1, 2 which have manual override logic.
    if (relayIndex >= 0 && relayIndex < 3) {
        if (_manualOverrideActive[relayIndex]) {
            _manualOverrideActive[relayIndex] = false;
            DEBUG_PRINTF(2, "RelayController: Manual R%d override cancelled by direct setState.\n", relayIndex + 1);
        }
    }

    if (_states[relayIndex] != state) {
        _states[relayIndex] = state;
        digitalWrite(_pins[relayIndex], _states[relayIndex] ? LOW : HIGH); // LOW to activate
        DEBUG_PRINTF(3, "RelayController: R%d set to %s (Direct)\n", relayIndex + 1, _states[relayIndex] ? "ON" : "OFF");
    }
}

bool RelayController::getState(int relayIndex) const {
    if (relayIndex < 0 || relayIndex >= 4) { // NUM_RELAYS is effectively 4 (indices 0-3)
        DEBUG_PRINTF(1, "RelayController Error: getState called with invalid index %d\n", relayIndex);
        return false; // Return a safe default (OFF) for invalid index
    }
    return _states[relayIndex];
}