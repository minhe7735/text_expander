#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/expansion_engine.h>
#include <zmk/hid_utils.h>

#include <zmk/text_expander_internals.h>

LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

static struct expansion_work expansion_work_item;

struct expansion_work *get_expansion_work_item(void) { return &expansion_work_item; }

void cancel_current_expansion(void) { k_work_cancel_delayable(&expansion_work_item.work); }

void expansion_work_handler(struct k_work *work) {
  struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
  struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);

  if (exp_work->is_backspace_phase) {
    if (exp_work->backspace_count > 0) {
      LOG_DBG("Sending backspace (remaining: %d)", exp_work->backspace_count);

      int ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
      if (ret < 0) {
        LOG_ERR("Failed to send backspace press: %d. Aborting expansion.", ret);
        return;
      }
      k_msleep(TYPING_DELAY / 2);

      ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
      if (ret < 0) {
        LOG_ERR("Failed to send backspace release: %d. Aborting expansion.", ret);
        return;
      }
      k_msleep(TYPING_DELAY / 2);

      exp_work->backspace_count--;

      k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
    } else {
      LOG_DBG("Backspace phase completed. Starting typing phase.");
      exp_work->is_backspace_phase = false;
      exp_work->text_index = 0;

      k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY * 2));
    }
  } else {
    if (exp_work->expanded_text[exp_work->text_index] != '\0' &&
        exp_work->text_index < CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN) {
      char c = exp_work->expanded_text[exp_work->text_index];
      bool needs_shift = false;
      uint32_t keycode = char_to_keycode(c, &needs_shift);

      if (keycode != 0) {
        LOG_DBG("Typing character: '%c' (keycode: 0x%x, shift: %s)", c, keycode,
                needs_shift ? "yes" : "no");

        int ret;
        if (needs_shift) {
          ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true);
          if (ret < 0) {
            LOG_ERR("Failed to press Shift. Aborting expansion for char '%c'.", c);
            return;
          }
          k_msleep(TYPING_DELAY / 4);
        }

        ret = send_and_flush_key_action(keycode, true);
        if (ret < 0) {
          LOG_ERR("Failed to press keycode 0x%x for char '%c'. Aborting.", keycode, c);

          if (needs_shift)
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
          return;
        }
        k_msleep(TYPING_DELAY / 2);

        ret = send_and_flush_key_action(keycode, false);
        if (ret < 0) {
          LOG_ERR("Failed to release keycode 0x%x for char '%c'. Shift might remain pressed.",
                  keycode, c);

          if (needs_shift)
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
          return;
        }

        if (needs_shift) {
          k_msleep(TYPING_DELAY / 4);

          ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
          if (ret < 0) {
            LOG_ERR("Failed to release Shift after char '%c'.", c);
          }
        }
      } else {
        LOG_WRN("Skipping unsupported character '%c' (0x%02x) during typing.", c, c);
      }

      exp_work->text_index++;

      k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
    } else {
      LOG_INF("Text expansion completed for '%s'", exp_work->expanded_text);
    }
  }
}

int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len) {
  cancel_current_expansion();

  strncpy(expansion_work_item.expanded_text, expanded_text,
          CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1);
  expansion_work_item.expanded_text[CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1] = '\0';

  expansion_work_item.backspace_count = short_len;
  expansion_work_item.is_backspace_phase = true;
  expansion_work_item.text_index = 0;

  LOG_INF("Initiating expansion of '%s' (backspaces: %d) to '%s'", short_code, short_len,
          expansion_work_item.expanded_text);

  k_work_reschedule(&expansion_work_item.work, K_MSEC(10));

  return 0;
}
