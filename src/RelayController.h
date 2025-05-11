/**
 * @file RelayController.h
 * @brief Defines the `RelayController` class for managing up to 4 physical relays,
 *        which are assumed to be low-state activated (LOW turns ON, HIGH turns OFF).
 *
 * This file declares the `RelayController` class, responsible for abstracting the
 * control of up to four hardware relays. The GPIO pins for these relays are defined
 * in `config.h` (e.g., `RELAY_CH1_PIN`, `RELAY_CH2_PIN`, etc.).
 *
 * Key functionalities of the `RelayController` include:
 * - Initialization (`begin()`): Configures the specified GPIO pins as outputs and
 *   sets all relays to an initial OFF state.
 * - Automated Control (`updateSingleRelayState()`): Updates the state of designated relays
 *   (typically relays 0, 1, and 2) based on environmental sensor readings (temperature, humidity)
 *   and predefined thresholds. This logic is bypassed if a manual override is active for the relay.
 * - Manual Override (`setManualOverride()`): Allows temporary, direct control over a
 *   relay's state (ON/OFF) for a specified duration, overriding the automated logic for
 *   relays 0, 1, and 2.
 * - State Management (`setState()`, `getState()`): Tracks the logical state (ON/OFF) of
 *   each relay and translates this to the appropriate physical pin state (LOW for ON, HIGH for OFF).
 * - Safe State (`forceSafeState()`): Provides a mechanism to immediately turn OFF all
 *   controlled relays (0-3) and cancel any active manual overrides.
 * - Status Reporting: Interacts with an `LCDDisplay` object (provided during construction)
 *   to display status messages or relay state changes.
 *
 * The relays are managed using a 0-based index (0 to 3):
 * - Relay 0 (Channel 1, pin `RELAY_CH1_PIN`): Typically an Exhaust Fan.
 * - Relay 1 (Channel 2, pin `RELAY_CH2_PIN`): Typically a Dehumidifier.
 * - Relay 2 (Channel 3, pin `RELAY_CH3_PIN`): Typically a Blower Fan.
 * - Relay 3 (Channel 4, pin `RELAY_CH4_PIN`): Often unused or reserved, with specific handling
 *   like `ensureRelay4Off()` and is not part of the automated control in `updateSingleRelayState()`.
 */
#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>    // Core Arduino framework for `pinMode`, `digitalWrite`, `millis()`, etc.
#include "LCDDisplay.h" // For displaying status messages (e.g., "Relay 1 ON").
#include "config.h"     // Provides relay GPIO pin definitions (`RELAY_CH1_PIN` to `RELAY_CH4_PIN`),
                        // debug macros (`DEBUG_RELAY`), and potentially default control thresholds.

/**
 * @class RelayController
 * @brief Manages the control and state of up to 4 physical, low-state activated relays.
 *
 * This class provides a comprehensive interface to initialize and control hardware relays
 * connected to GPIO pins defined in `config.h`. It operates on the assumption that relays
 * are **low-state activated** (i.e., setting the GPIO pin to `LOW` turns the relay ON, and
 * setting it to `HIGH` turns the relay OFF).
 * The class maintains the *logical* state of each relay (`true` for ON, `false` for OFF)
 * and handles the necessary translation to the physical pin state. It supports both automated
 * control based on sensor inputs and manual override capabilities.
 */
class RelayController {
public:
    /**
     * @brief Constructor for `RelayController`.
     * Initializes the internal array of relay pin numbers based on definitions
     * from `config.h` (`RELAY_CH1_PIN` to `RELAY_CH4_PIN`). Stores a reference
     * to an `LCDDisplay` object for status reporting.
     *
     * @param d Reference to an `LCDDisplay` object. This display will be used for
     *          outputting status messages related to relay operations (e.g., "Relay 1 ON",
     *          "Override Active").
     */
    RelayController(LCDDisplay& d);

    /**
     * @brief Initializes the relay hardware pins and their default states.
     * This method **must** be called once (typically in the `setup()` function)
     * before any other relay operations are performed.
     * For each of the 4 relays, it:
     * 1. Sets the corresponding GPIO pin (e.g., `RELAY_CH1_PIN` for relay 0, as defined
     *    in `config.h`) to `OUTPUT` mode using `pinMode()`.
     * 2. Sets the initial logical state of the relay to OFF (`_states[i] = false`).
     * 3. Writes the physical pin state to `HIGH` (which corresponds to OFF for
     *    low-state activated relays) using `digitalWrite()`.
     */
    void begin();

    /**
     * @brief Updates the logical state of a single specified relay (index 0, 1, or 2)
     *        based on sensor values and predefined thresholds, unless a manual override is active.
     *
     * This function implements the core automated control logic for the primary relays.
     * It first checks if a manual override is active for the `relayIndex`. If not, it applies
     * control rules specific to the relay:
     * - **Relay 0 (Exhaust Fan, index 0):** Typically turned ON if `tempValue` exceeds `tempMax`
     *   OR `humidityValue` exceeds `humidityMax`. Turned OFF otherwise.
     * - **Relay 1 (Dehumidifier, index 1):** Typically turned ON if `humidityValue` exceeds `humidityMax`.
     *   Turned OFF otherwise.
     * - **Relay 2 (Blower Fan, index 2):** Typically turned ON if `tempValue` exceeds `tempMax`.
     *   Turned OFF otherwise.
     * (The exact conditions are implemented in the corresponding `.cpp` file and may use `tempMin` and `humidityMin` for turn-off logic.)
     *
     * Relay 3 (index 3, typically "Relay 4") is **not** managed by this automated logic;
     * use `setState(3, ...)` or `ensureRelay4Off()` for its control.
     *
     * If the state changes, it updates the internal logical state and the physical pin via `setState()`.
     *
     * @param relayIndex The 0-based index of the relay to update. Must be 0, 1, or 2.
     *                   - `0`: Controls Relay 1 (Exhaust Fan).
     *                   - `1`: Controls Relay 2 (Dehumidifier).
     *                   - `2`: Controls Relay 3 (Blower Fan).
     * @param humidityValue Current ambient humidity reading (e.g., in percent).
     * @param humidityMin Minimum humidity threshold (may be used for hysteresis or to turn OFF if below min).
     * @param humidityMax Maximum humidity threshold (e.g., turn ON if humidity is above this value).
     * @param tempValue Current ambient temperature reading (e.g., in Celsius).
     * @param tempMin Minimum temperature threshold (may be used for hysteresis or to turn OFF if below min).
     * @param tempMax Maximum temperature threshold (e.g., turn ON if temperature is above this value).
     *
     * @return `true` if the logical state of the specified relay was changed by this function call
     *         (and no manual override was active).
     * @return `false` if the state did not change, if `relayIndex` is out of bounds (not 0, 1, or 2),
     *         or if a manual override is currently active for the specified relay.
     */
    bool updateSingleRelayState(int relayIndex, float humidityValue, float humidityMin, float humidityMax,
                                float tempValue, float tempMin, float tempMax);

    /**
     * @brief Explicitly ensures that Relay 4 (identified by index 3) is turned OFF.
     * This function directly calls `setState(3, false)` to set the logical state of
     * the fourth relay to OFF and update its physical pin accordingly.
     * It's often used as a safety measure or for relays with specific operational requirements
     * that are not part of the general automated control handled by `updateSingleRelayState()`.
     */
    void ensureRelay4Off();

    /**
     * @brief Sets a manual override for a specific relay (index 0, 1, or 2),
     *        forcing it to a desired state for a specified duration.
     *
     * This function allows direct, temporary control of a relay, bypassing the automated
     * logic in `updateSingleRelayState()` for the duration of the override.
     * When an override is activated:
     * - The specified relay (0, 1, or 2) is immediately set to `desiredState` using `setState()`.
     * - Subsequent calls to `updateSingleRelayState()` for this relay will not alter its state
     *   until the override expires.
     * - The override automatically expires after `durationMs` milliseconds. Once expired,
     *   the relay reverts to automated control during the next relevant `updateSingleRelayState()` call.
     *
     * A new call to `setManualOverride()` for the same relay will replace any existing override for that relay.
     * Manual overrides are **not supported** for Relay 4 (index 3).
     *
     * @param relayIndex The 0-based index of the relay to override. Must be 0, 1, or 2.
     *                   (0 for Exhaust Fan, 1 for Dehumidifier, 2 for Blower Fan).
     * @param desiredState The desired logical state for the relay during the override
     *                     (`true` for ON, `false` for OFF).
     * @param durationMs The duration of the manual override in milliseconds. If `durationMs` is 0,
     *                   any active override for the specified `relayIndex` is effectively cancelled,
     *                   and the relay immediately returns to automated control.
     */
    void setManualOverride(int relayIndex, bool desiredState, unsigned long durationMs);

    /**
     * @brief Forces all controlled relays (Relays 1 through 4, indices 0 through 3) to their
     *        safe state (OFF) and cancels all active manual overrides.
     *
     * This function performs the following critical actions:
     * 1. Immediately cancels any active manual overrides for relays 0, 1, and 2 by setting
     *    their `_manualOverrideActive` flags to `false`.
     * 2. Sets the logical state of relays 0, 1, and 2 to OFF (`false`) by calling `setState(i, false)`.
     * 3. Calls `ensureRelay4Off()` which effectively calls `setState(3, false)` to also turn off Relay 4.
     * This method is typically used in critical situations, for system shutdown sequences, or
     * to ensure a known safe state.
     */
    void forceSafeState();

    /** @brief Gets the current logical state of Relay 1 (Exhaust Fan, controlled by index 0). @return `true` if logically ON, `false` if OFF. Wrapper for `getState(0)`. */
    bool getR1() const;
    /** @brief Gets the current logical state of Relay 2 (Dehumidifier, controlled by index 1). @return `true` if logically ON, `false` if OFF. Wrapper for `getState(1)`. */
    bool getR2() const;
    /** @brief Gets the current logical state of Relay 3 (Blower Fan, controlled by index 2). @return `true` if logically ON, `false` if OFF. Wrapper for `getState(2)`. */
    bool getR3() const;
    /** @brief Gets the current logical state of Relay 4 (typically Unused/Reserved, controlled by index 3). @return `true` if logically ON, `false` if OFF. Wrapper for `getState(3)`. */
    bool getR4() const;

    /**
     * @brief Sets the logical state of a specific relay by its 0-based index and updates its physical pin.
     *
     * This method directly controls a relay's state, regardless of automated logic or overrides
     * (though overrides use this method internally). It first performs bounds checking on `relayIndex` (0-3).
     * If `relayIndex` is valid:
     * 1. Updates the internal logical state: `_states[relayIndex] = state`.
     * 2. Translates this logical state to the physical pin state for low-state activated relays:
     *    - Logical ON (`true`)  => Physical pin state `LOW`.
     *    - Logical OFF (`false`) => Physical pin state `HIGH`.
     * 3. Writes this physical state to the GPIO pin `_pins[relayIndex]` using `digitalWrite()`.
     *
     * @param relayIndex The 0-based index of the relay to control (0-3):
     *                   - `0`: Relay 1 (Exhaust Fan, pin `RELAY_CH1_PIN` from `config.h`)
     *                   - `1`: Relay 2 (Dehumidifier, pin `RELAY_CH2_PIN` from `config.h`)
     *                   - `2`: Relay 3 (Blower Fan, pin `RELAY_CH3_PIN` from `config.h`)
     *                   - `3`: Relay 4 (Unused/Reserved, pin `RELAY_CH4_PIN` from `config.h`)
     * @param state The desired logical state for the relay: `true` to turn the relay ON,
     *              `false` to turn it OFF. If `relayIndex` is out of bounds (not 0-3),
     *              no action is taken.
     */
    void setState(int relayIndex, bool state);

    /**
     * @brief Gets the current logical state of a specific relay by its 0-based index.
     *
     * Performs bounds checking on `relayIndex` (0-3).
     * @param relayIndex The 0-based index of the relay whose state is to be queried (0-3).
     * @return `true` if the relay at the specified `relayIndex` is logically ON (`_states[relayIndex] == true`).
     * @return `false` if the relay is logically OFF (`_states[relayIndex] == false`), or if
     *         `relayIndex` is out of bounds (not 0, 1, 2, or 3).
     */
    bool getState(int relayIndex) const;

private:
    /**
     * @brief Array storing the GPIO pin numbers for each of the 4 relays.
     * Initialized in the constructor using values from `config.h`:
     * `_pins[0]` = `RELAY_CH1_PIN`, `_pins[1]` = `RELAY_CH2_PIN`,
     * `_pins[2]` = `RELAY_CH3_PIN`, `_pins[3]` = `RELAY_CH4_PIN`.
     */
    int _pins[4];
    /**
     * @brief Array tracking the current *logical* state of each of the 4 relays.
     * - `_states[i] == true`: Relay `i` is logically ON.
     * - `_states[i] == false`: Relay `i` is logically OFF.
     * This logical state is translated to a physical pin state (`LOW` for ON, `HIGH` for OFF)
     * by the `setState()` method, due to the low-state activation nature of the relays.
     */
    bool _states[4];
    
    // --- Manual Override State Variables (apply only to relays 0, 1, and 2) ---
    /**
     * @brief Array of flags indicating if a manual override is currently active.
     * `_manualOverrideActive[i] == true` means relay `i` (where `i` is 0, 1, or 2)
     * is under manual control and `updateSingleRelayState` will not change its state.
     * Relay 3 (index 3) does not support manual override through this mechanism.
     */
    bool _manualOverrideActive[3]; // Index 0 for Relay 0, 1 for Relay 1, 2 for Relay 2
    /**
     * @brief Array storing the target logical state (`true` for ON, `false` for OFF)
     * for a relay during an active manual override.
     * `_manualOverrideTargetState[i]` is relevant only if `_manualOverrideActive[i]` is `true`.
     * Applies to relays 0, 1, and 2.
     */
    bool _manualOverrideTargetState[3]; // Index 0 for Relay 0, 1 for Relay 1, 2 for Relay 2
    /**
     * @brief Array storing the `millis()` timestamp when an active manual override
     * should automatically expire.
     * Compared against the current `millis()` in `updateSingleRelayState()` to determine
     * if an override for relay `i` (where `i` is 0, 1, or 2) should be terminated.
     */
    unsigned long _manualOverrideEndTime[3]; // Index 0 for Relay 0, 1 for Relay 1, 2 for Relay 2

    /**
     * @brief Reference to the `LCDDisplay` object provided during construction.
     * Used for displaying status messages, errors, or relay state changes to the user
     * via the LCD. For example, `_lcd.displayStatus("Relay 1 ON")`.
     */
    LCDDisplay& _lcd;
};

#endif // RELAY_CONTROLLER_H