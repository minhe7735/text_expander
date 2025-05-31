#include <zephyr/kernel.h>      // For basic Zephyr types, not strictly needed for this file's direct logic but common.
#include <zephyr/logging/log.h> // For Zephyr's logging API (LOG_ERR, LOG_WRN).

#include <zmk/hid_utils.h> // Header for this module's public API.
// zmk/hid.h and zmk/endpoints.h are included via hid_utils.h for:
// - zmk_hid_keyboard_press(), zmk_hid_keyboard_release()
// - zmk_endpoints_send_report()
// - HID Usage ID macros (e.g., HID_USAGE_KEY_KEYBOARD_A, HID_USAGE_KEY_KEYBOARD_SPACEBAR)

// Define a logging module for this file, consistent with other text_expander files.
LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

/**
 * @brief Sends a key press or release event to the ZMK HID subsystem.
 *
 * This is a simple wrapper around ZMK's core HID functions.
 *
 * @param keycode The HID keycode to send.
 * @param pressed True to indicate a key press, false for a key release.
 * @return Result of the zmk_hid_keyboard_press/release call (0 on success, negative on error).
 */
static int send_key_action(uint32_t keycode, bool pressed) {
    if (pressed) {
        return zmk_hid_keyboard_press(keycode);
    } else {
        return zmk_hid_keyboard_release(keycode);
    }
}

/**
 * @brief Sends a key action (press or release) and then immediately flushes the HID report.
 *
 * This ensures the host receives the key event quickly, which is crucial for responsive typing
 * during text expansion.
 *
 * @param keycode The HID keycode for the action.
 * @param pressed True for key press, false for key release.
 * @return 0 on success, or a negative error code if either the key action or flushing the report fails.
 */
int send_and_flush_key_action(uint32_t keycode, bool pressed) {
    // Perform the key press or release.
    int ret = send_key_action(keycode, pressed);
    if (ret < 0) {
        LOG_ERR("Failed to %s keycode 0x%x: %d",
                pressed ? "press" : "release", keycode, ret);
        return ret; // Return the error code from send_key_action.
    }

    // Send (flush) the HID report to the host.
    // HID_USAGE_KEY indicates it's a keyboard report.
    ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    if (ret < 0) {
        LOG_ERR("Failed to send HID report: %d", ret);
        return ret; // Return the error code from zmk_endpoints_send_report.
    }

    return 0; // Success.
}

/**
 * @brief Converts an ASCII character to its corresponding HID keycode and determines if Shift is needed.
 *
 * Maps common printable ASCII characters to their USB HID Keyboard page usage IDs.
 * For characters requiring Shift (e.g., 'A', '!'), it sets `*needs_shift` to true.
 *
 * @param c The character to convert.
 * @param needs_shift Output parameter; set to true if Shift modifier is required, false otherwise.
 * @return The HID keycode. Returns 0 if the character is unsupported.
 */
uint32_t char_to_keycode(char c, bool *needs_shift) {
    *needs_shift = false; // Assume Shift is not needed by default.

    // Handle lowercase letters 'a' through 'z'.
    if (c >= 'a' && c <= 'z') {
        return HID_USAGE_KEY_KEYBOARD_A + (c - 'a');
    }
    // Handle uppercase letters 'A' through 'Z'.
    else if (c >= 'A' && c <= 'Z') {
        *needs_shift = true; // Uppercase requires Shift.
        return HID_USAGE_KEY_KEYBOARD_A + (c - 'A');
    }
    // Handle digits '0' through '9'.
    else if (c >= '0' && c <= '9') {
        if (c == '0') return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
        return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (c - '1');
    }

    // Handle common symbols and whitespace.
    // This switch statement covers many US-layout symbols.
    // For other layouts or more comprehensive symbol support, this map would need expansion.
    switch (c) {
    case ' ':  return HID_USAGE_KEY_KEYBOARD_SPACEBAR;
    case '.':  return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
    case ',':  return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
    case ':':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
    case ';':  return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
    case '!':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
    case '@':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_2_AND_AT;
    case '#':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_3_AND_HASH;
    case '$':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR;
    case '%':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT;
    case '^':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_6_AND_CARET;
    case '&':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND;
    case '*':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK;
    case '(':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS;
    case ')':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
    case '-':  return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
    case '_':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
    case '=':  return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
    case '+':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
    case '\n': return HID_USAGE_KEY_KEYBOARD_RETURN_ENTER; // Newline character.
    case '\t': return HID_USAGE_KEY_KEYBOARD_TAB;          // Tab character.
    case '[':  return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
    case ']':  return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
    case '{':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
    case '}':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
    case '\\': return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
    case '|':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
    case '\'': return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE; // Single quote.
    case '"':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE; // Double quote.
    case '`':  return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
    case '~':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
    case '/':  return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
    case '?':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
    case '<':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
    case '>':  *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
    default:
        // If the character is not recognized, log a warning and return 0 (no keycode).
        LOG_WRN("Unsupported character for typing: '%c' (0x%02x)", c, c);
        return 0;
    }
}
