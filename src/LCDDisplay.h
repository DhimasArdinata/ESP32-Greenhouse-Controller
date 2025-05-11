#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "config.h" // For LCD_ADDR

/**
 * @brief Manages operations for an I2C LCD 20x4 display.
 *
 * This class encapsulates the `LiquidCrystal_I2C` library to provide a simplified
 * interface for initializing the display, updating it with formatted sensor data
 * and system status, displaying custom messages, and controlling basic LCD functions
 * like clearing the screen or setting the cursor position. It relies on constants
 * defined in `config.h` for LCD address, dimensions, and initial messages.
 */
class LCDDisplay {
public:
    /**
     * @brief Constructor for LCDDisplay.
     * Initializes the underlying `LiquidCrystal_I2C` object using `LCD_ADDR`, `LCD_COLS`,
     * and `LCD_ROWS` constants defined in `config.h`.
     */
    LCDDisplay();

    /**
     * @brief Initializes the LCD.
     * Calls `_lcd_i2c.init()` and `_lcd_i2c.backlight()`.
     * Then, it clears the screen and prints an initial loading message, typically
     * defined by `LCD_INITIAL_MESSAGE_LINE1` and `LCD_INITIAL_MESSAGE_LINE2` from `config.h`.
     */
    void begin();

    /**
     * @brief Updates the entire LCD screen with formatted current data and status indicators.
     * This method typically clears the screen and then lays out various pieces of information
     * across the 4 lines of the 20-column display.
     *
     * @param dt Current date and time string, expected in "YYYY-MM-DD HH:MM:SS" format.
     * @param temp Current temperature value (e.g., in Celsius).
     * @param hum Current humidity value (e.g., in %RH).
     * @param light Current light intensity value (units depend on sensor, e.g., Lux or raw).
     * @param r1 Status of relay 1 (Exhaust). `true` for ON, `false` for OFF.
     * @param r2 Status of relay 2 (Dehumidifier). `true` for ON, `false` for OFF.
     * @param r3 Status of relay 3 (Blower). `true` for ON, `false` for OFF.
     * @param r4 Status of relay 4 (Unused/Spare). `true` for ON, `false` for OFF.
     * @param tMin Temperature minimum threshold.
     * @param tMax Temperature maximum threshold.
     * @param humMin Humidity minimum threshold.
     * @param humMax Humidity maximum threshold.
     * @param lightMin Light minimum threshold (Note: current implementation might not display this on the main update screen).
     * @param lightMax Light maximum threshold (Note: current implementation might not display this on the main update screen).
     * @param netConnected `true` if the primary network (WiFi/GPRS) is connected, `false` otherwise.
     * @param isDataStale `true` if the data from an external API is considered old or not recently updated.
     * @param sdCardOkLocal `true` if the SD card is initialized and functioning correctly.
     * @param isInFailSafe `true` if the system is operating in a failsafe mode due to critical errors.
     */
    void update(const char* dt, float temp, float hum, float light, bool r1, bool r2, bool r3, bool r4,
                float tMin, float tMax, float humMin, float humMax, float lightMin, float lightMax,
                bool netConnected, bool isDataStale, bool sdCardOkLocal, bool isInFailSafe);

    /**
     * @brief Displays a message at a specific column and row on the LCD.
     * The message will be truncated if it exceeds the display width from the starting column.
     *
     * @param col Column to start displaying the message (0-indexed, typically 0-19 for a 20x4 LCD).
     * @param row Row to display the message (0-indexed, typically 0-3 for a 20x4 LCD).
     * @param msg The C-style string message to display. Ensure it's null-terminated.
     *            The message should not exceed `LCD_COLS - col` characters to fit on one line.
     * @param clearLine If `true`, the entire specified row will be cleared by printing spaces
     *                  before the new message is printed. Defaults to `false`.
     */
    void message(int col, int row, const char* msg, bool clearLine = false);

    /**
     * @brief Clears the entire LCD screen and homes the cursor to (0,0).
     * Calls `_lcd_i2c.clear()`.
     */
    void clear();

    /**
     * @brief Sets the cursor position on the LCD for subsequent print operations.
     * Calls `_lcd_i2c.setCursor(col, row)`.
     * @param col Column position (0-indexed).
     * @param row Row position (0-indexed).
     */
    void setCursor(int col, int row);

    /**
     * @brief Prints a C-style string to the LCD at the current cursor position.
     * Wraps `_lcd_i2c.print(const char*)`.
     * @param msg The null-terminated C-style string to print.
     */
    void print(const char* msg);

    /**
     * @brief Prints a string from flash memory (using the `F()` macro) to the LCD at the current cursor position.
     * Wraps `_lcd_i2c.print(const __FlashStringHelper*)`.
     * @param msg The flash string helper (e.g., `F("Hello")`) to print.
     */
    void print(const __FlashStringHelper* msg);

private:
    LiquidCrystal_I2C _lcd_i2c; ///< Instance of the `LiquidCrystal_I2C` library.
                                ///< It is initialized in the `LCDDisplay` constructor with the I2C address
                                ///< (`LCD_ADDR`), number of columns (`LCD_COLS`), and number of rows (`LCD_ROWS`)
                                ///< defined in `config.h`.
};

#endif // LCD_DISPLAY_H