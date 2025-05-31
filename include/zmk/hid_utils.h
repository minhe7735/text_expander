#ifndef ZMK_HID_UTILS_H // Start of include guard.
#define ZMK_HID_UTILS_H

#include <stdint.h>  // For uint32_t.
#include <stdbool.h> // For bool type.
#include <zmk/hid.h> // For ZMK HID definitions like zmk_hid_keyboard_press/release.
#include <zmk/endpoints.h> // For zmk_endpoints_send_report and HID_USAGE_KEY.

/**
 * @brief Converts a character to its corresponding HID keycode and determines if Shift is needed.
 *
 * This function maps common printable ASCII characters to USB HID keycodes.
 * For characters that require the Shift modifier (e.g., uppercase letters, symbols like '!', '@'),
 * it sets the needs_shift flag.
 *
 * @param c The character to convert.
 * @param needs_shift Pointer to a boolean that will be set to true if Shift is required for the character,
 * false otherwise.
 * @return The HID keycode for the character. Returns 0 if the character is not supported or
 * if it's a non-printable control character not handled here.
 */
uint32_t char_to_keycode(char c, bool *needs_shift);

/**
 * @brief Sends a key press or release action and flushes the HID report.
 *
 * This function is a convenience wrapper that calls the appropriate ZMK HID function
 * (press or release) and then immediately sends the HID report to the host. This ensures
 * that the key action is processed by the host without needing to wait for the next
 * automatic report interval.
 *
 * @param keycode The HID keycode to send.
 * @param pressed True to send a key press, false to send a key release.
 * @return 0 on success, or a negative error code from ZMK's HID functions if sending fails.
 */
int send_and_flush_key_action(uint32_t keycode, bool pressed);

#endif // ZMK_HID_UTILS_H End of include guard.
