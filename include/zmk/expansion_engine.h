#ifndef ZMK_EXPANSION_ENGINE_H // Start of include guard to prevent multiple inclusions.
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h> // Includes Zephyr kernel definitions (e.g., k_work_delayable).
#include <stdint.h>        // Includes standard integer types (e.g., uint8_t).
#include <stdbool.h>       // Includes boolean type (bool).

// The Kconfig options CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN and
// CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY directly control the behavior of this module.
// Their default values are typically managed in Kconfig and referenced via
// text_expander_internals.h for consistency.

// Make sure text_expander_internals.h is included if direct access to MAX_EXPANDED_LEN or TYPING_DELAY macros is needed
// or rely on Kconfig values being available.
// For this module's .c file, it typically includes text_expander_internals.h or relies on Kconfig.

/**
 * @brief Structure to manage the state of an ongoing text expansion.
 *
 * This structure holds all necessary information for the expansion process,
 * including the text to be typed, the number of backspaces needed,
 * and the current phase of the expansion (backspacing or typing).
 */
struct expansion_work {
    struct k_work_delayable work;         // Zephyr work item for scheduling expansion tasks.
                                          // Allows parts of the expansion (like typing each char)
                                          // to be done asynchronously without blocking.
    char expanded_text[CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN]; // Buffer to store the full text to be typed out.
    uint8_t backspace_count;              // Number of backspace characters to send to delete the short code.
    bool is_backspace_phase;              // Flag indicating if the engine is currently sending backspaces.
                                          // True if backspacing, false if typing the expanded text.
    size_t text_index;                    // Current index into expanded_text being typed.
};

/**
 * @brief Handler function for the expansion work item.
 *
 * This function is executed when the k_work_delayable item is scheduled.
 * It manages the step-by-step process of sending backspaces and typing characters.
 *
 * @param work Pointer to the k_work structure (part of k_work_delayable).
 */
void expansion_work_handler(struct k_work *work);

/**
 * @brief Starts the text expansion process.
 *
 * Initializes the expansion_work item with the provided short code and expanded text,
 * and schedules the first part of the expansion (backspacing).
 *
 * @param short_code The short code string that triggered the expansion (used for logging).
 * @param expanded_text The text string to be typed out.
 * @param short_len The length of the short_code, indicating how many backspaces are needed.
 * @return 0 on success, or a negative error code if initialization fails.
 */
int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len);

/**
 * @brief Cancels any ongoing text expansion.
 *
 * If an expansion is in progress (either backspacing or typing), this function
 * will stop it.
 */
void cancel_current_expansion(void);

/**
 * @brief Retrieves a pointer to the global expansion_work item.
 *
 * This allows other parts of the text expander module to access and interact
 * with the expansion engine's state.
 *
 * @return Pointer to the static expansion_work item.
 */
struct expansion_work *get_expansion_work_item(void);

#endif // ZMK_EXPANSION_ENGINE_H End of include guard.
