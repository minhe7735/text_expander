#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/hid_utils.h>

LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

static int send_key_action(uint32_t keycode, bool pressed) {
  if (pressed) {
    return zmk_hid_keyboard_press(keycode);
  } else {
    return zmk_hid_keyboard_release(keycode);
  }
}

int send_and_flush_key_action(uint32_t keycode, bool pressed) {
  int ret = send_key_action(keycode, pressed);
  if (ret < 0) {
    LOG_ERR("Failed to %s keycode 0x%x: %d", (pressed ? "press" : "release"), keycode, ret);
    return ret;
  }

  ret = zmk_endpoints_send_report(HID_USAGE_KEY);
  if (ret < 0) {
    LOG_ERR("Failed to send HID report: %d", ret);
    return ret;
  }

  return 0;
}

uint32_t char_to_keycode(char c, bool *needs_shift) {
  *needs_shift = false;

  if (c >= 'a' && c <= 'z') {
    return HID_USAGE_KEY_KEYBOARD_A + (c - 'a');
  }

  else if (c >= 'A' && c <= 'Z') {
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_A + (c - 'A');
  }

  else if (c >= '0' && c <= '9') {
    if (c == '0')
      return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
    return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (c - '1');
  }

  switch (c) {
  case ' ':
    return HID_USAGE_KEY_KEYBOARD_SPACEBAR;
  case '.':
    return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
  case ',':
    return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
  case ':':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
  case ';':
    return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
  case '!':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
  case '@':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_2_AND_AT;
  case '#':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_3_AND_HASH;
  case '$':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR;
  case '%':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT;
  case '^':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_6_AND_CARET;
  case '&':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND;
  case '*':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK;
  case '(':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS;
  case ')':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
  case '-':
    return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
  case '_':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
  case '=':
    return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
  case '+':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
  case '\n':
    return HID_USAGE_KEY_KEYBOARD_RETURN_ENTER;
  case '\t':
    return HID_USAGE_KEY_KEYBOARD_TAB;
  case '[':
    return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
  case ']':
    return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
  case '{':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
  case '}':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
  case '\\':
    return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
  case '|':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
  case '\'':
    return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE;
  case '"':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE;
  case '`':
    return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
  case '~':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
  case '/':
    return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
  case '?':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
  case '<':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
  case '>':
    *needs_shift = true;
    return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
  default:
    LOG_WRN("Unsupported character for typing: '%c' (0x%02x)", c, c);
    return 0;
  }
}
