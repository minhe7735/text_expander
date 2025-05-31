#include <zephyr/kernel.h>      // For k_work, k_work_delayable, k_msleep, CONTAINER_OF, etc.
#include <zephyr/logging/log.h> // For Zephyr's logging API (LOG_DBG, LOG_INF, etc.).
#include <string.h>             // For strncpy.

#include <zmk/expansion_engine.h> // Header for this module's public API and definitions.
#include <zmk/hid_utils.h>        // For send_and_flush_key_action, char_to_keycode.
                                  // Also for HID usage page definitions (e.g., HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE).
#include <zmk/text_expander_internals.h> 

// Define a logging module for this file.
// The name "zmk_behavior_text_expander" should match the one used in other text expander files
// for consistent log filtering. CONFIG_ZMK_LOG_LEVEL controls the verbosity.
LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

// Static instance of the expansion_work structure.
// This holds the state of the current (or next) text expansion.
// Only one expansion can be processed at a time by this engine.
static struct expansion_work expansion_work_item;

/**
 * @brief Returns a pointer to the static expansion_work_item.
 * Allows other parts of the system to get a reference to the expansion work data.
 */
struct expansion_work *get_expansion_work_item(void) {
    return &expansion_work_item;
}

/**
 * @brief Cancels any scheduled work for the expansion_work_item.
 * This effectively stops any ongoing or pending expansion.
 */
void cancel_current_expansion(void) {
    k_work_cancel_delayable(&expansion_work_item.work);
}

/**
 * @brief Work handler function that performs the text expansion steps.
 *
 * This function is called by the Zephyr kernel when the delayable work item
 * expansion_work_item.work is scheduled and its delay expires. It handles
 * two phases:
 * 1. Backspace phase: Sends backspace key presses to delete the typed short code.
 * 2. Typing phase: Types out the characters of the expanded text.
 *
 * @param work Pointer to the struct k_work embedded in expansion_work_item.
 */
void expansion_work_handler(struct k_work *work) {
    // Retrieve the containing expansion_work structure from the k_work pointer.
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
    struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);

    if (exp_work->is_backspace_phase) {
        // --- Backspace Phase ---
        if (exp_work->backspace_count > 0) {
            LOG_DBG("Sending backspace (remaining: %d)", exp_work->backspace_count);

            // Send a backspace key press.
            int ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true); // Press
            if (ret < 0) {
                LOG_ERR("Failed to send backspace press: %d. Aborting expansion.", ret);
                return; // Abort if HID send fails.
            }
            k_msleep(TYPING_DELAY / 2); // Short delay between press and release.

            // Send a backspace key release.
            ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false); // Release
            if (ret < 0) {
                LOG_ERR("Failed to send backspace release: %d. Aborting expansion.", ret);
                return; // Abort if HID send fails.
            }
            k_msleep(TYPING_DELAY / 2); // Delay before next action.

            exp_work->backspace_count--; // Decrement count of remaining backspaces.
            // Reschedule this handler to send the next backspace.
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
        } else {
            // Backspace phase is complete.
            LOG_DBG("Backspace phase completed. Starting typing phase.");
            exp_work->is_backspace_phase = false; // Switch to typing phase.
            exp_work->text_index = 0;             // Reset text index for typing.
            // Reschedule to start typing after a slightly longer pause.
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY * 2));
        }
    } else {
        // --- Typing Phase ---
        // Check if there are more characters to type and we are within buffer bounds.
        // Note: MAX_EXPANDED_LEN from text_expander_internals.h will be used for the struct's array size
        // via CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN.
        if (exp_work->expanded_text[exp_work->text_index] != '\0' &&
            exp_work->text_index < CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN) {

            char c = exp_work->expanded_text[exp_work->text_index]; // Get current character.
            bool needs_shift = false;
            uint32_t keycode = char_to_keycode(c, &needs_shift); // Convert char to HID keycode.

            if (keycode != 0) { // A keycode of 0 means the character is not supported for typing.
                LOG_DBG("Typing character: '%c' (keycode: 0x%x, shift: %s)",
                        c, keycode, needs_shift ? "yes" : "no");

                int ret;
                if (needs_shift) {
                    // Press Shift if needed for the character (e.g., uppercase, symbols).
                    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true); // Press Shift
                    if (ret < 0) {
                        LOG_ERR("Failed to press Shift. Aborting expansion for char '%c'.", c);
                        return;
                    }
                    k_msleep(TYPING_DELAY / 4); // Brief pause after Shift press.
                }

                // Press the character's key.
                ret = send_and_flush_key_action(keycode, true); // Press key
                if (ret < 0) {
                     LOG_ERR("Failed to press keycode 0x%x for char '%c'. Aborting.", keycode, c);
                    // Attempt to release shift if it was pressed
                    if (needs_shift) send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
                    return;
                }
                k_msleep(TYPING_DELAY / 2); // Pause while key is pressed.

                // Release the character's key.
                ret = send_and_flush_key_action(keycode, false); // Release key
                 if (ret < 0) {
                    LOG_ERR("Failed to release keycode 0x%x for char '%c'. Shift might remain pressed.", keycode, c);
                    // Attempt to release shift if it was pressed, but state might be inconsistent.
                    if (needs_shift) send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
                    return;
                }


                if (needs_shift) {
                    k_msleep(TYPING_DELAY / 4); // Brief pause before releasing Shift.
                    // Release Shift.
                    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false); // Release Shift
                    if (ret < 0) {
                         LOG_ERR("Failed to release Shift after char '%c'.", c);
                        // Continue with next char, but Shift might be stuck.
                    }
                }
            } else {
                // Log a warning if a character in the expanded text cannot be typed.
                LOG_WRN("Skipping unsupported character '%c' (0x%02x) during typing.", c, c);
            }

            exp_work->text_index++; // Move to the next character.
            // Reschedule this handler to type the next character.
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
        } else {
            // End of expanded text or buffer reached. Expansion is complete.
            LOG_INF("Text expansion completed for '%s'", exp_work->expanded_text);
            // No more rescheduling, work item becomes idle.
        }
    }
}

/**
 * @brief Initializes and starts the text expansion process.
 *
 * This function prepares the expansion_work_item with the text to be expanded
 * and the number of backspaces required to delete the short code. It then
 * schedules the expansion_work_handler to begin the process.
 *
 * @param short_code The original short code (used for logging).
 * @param expanded_text The text to type out.
 * @param short_len The length of the short_code, determining the number of backspaces.
 * @return 0 on success. (Currently always returns 0).
 */
int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len) {
    // Cancel any previously ongoing expansion to prevent conflicts.
    cancel_current_expansion();

    // Copy the expanded text into the work item's buffer.
    // Ensure null termination and protect against buffer overflow.
    // Note: expansion_work_item.expanded_text array size is defined by CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
    strncpy(expansion_work_item.expanded_text, expanded_text, CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1);
    expansion_work_item.expanded_text[CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1] = '\0'; // Ensure null termination.

    // Set up the initial state for the expansion.
    expansion_work_item.backspace_count = short_len;      // Number of backspaces to send.
    expansion_work_item.is_backspace_phase = true;        // Start with the backspace phase.
    expansion_work_item.text_index = 0;                   // Reset text index.

    LOG_INF("Initiating expansion of '%s' (backspaces: %d) to '%s'",
            short_code, short_len, expansion_work_item.expanded_text);

    // Schedule the expansion_work_handler to run after a very short delay (10ms).
    // This allows the current context (e.g., key press handler) to return quickly.
    k_work_reschedule(&expansion_work_item.work, K_MSEC(10)); // Initial delay before first step

    return 0; // Indicate success.
}
