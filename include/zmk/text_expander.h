#ifndef ZMK_BEHAVIOR_TEXT_EXPANDER_H // Start of include guard.
#define ZMK_BEHAVIOR_TEXT_EXPANDER_H

#include <zephyr/kernel.h> // For Zephyr specific types if needed by underlying implementations.
#include <stdbool.h>       // For bool type.

// Standard C++ extern "C" guard for compatibility if this header is included in C++ code.
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Adds a new text expansion or updates an existing one.
 *
 * Associates a short_code with an expanded_text. If the short_code already exists,
 * its corresponding expanded_text is updated.
 *
 * @param short_code The null-terminated string for the short code (e.g., "eml").
 * Must contain only lowercase letters (a-z) and numbers (0-9).
 * @param expanded_text The null-terminated string for the expanded text (e.g., "user@example.com").
 * @return 0 on success.
 * @return -EINVAL if short_code or expanded_text is NULL, or if their lengths are invalid,
 * or if short_code contains invalid characters.
 * @return -ENOMEM if there is not enough memory to store the new expansion.
 */
int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text);

/**
 * @brief Removes a text expansion.
 *
 * Deletes the expansion associated with the given short_code.
 *
 * @param short_code The null-terminated string for the short code to remove.
 * @return 0 on success.
 * @return -EINVAL if short_code is NULL.
 * @return -ENOENT if the short_code was not found.
 */
int zmk_text_expander_remove_expansion(const char *short_code);

/**
 * @brief Clears all stored text expansions.
 *
 * Removes all short_code to expanded_text mappings.
 */
void zmk_text_expander_clear_all(void);

/**
 * @brief Gets the current number of stored text expansions.
 *
 * @return The total count of active text expansions.
 */
int zmk_text_expander_get_count(void);

/**
 * @brief Checks if a text expansion exists for a given short code.
 *
 * @param short_code The null-terminated string for the short code to check.
 * @return True if an expansion exists for the short_code, false otherwise.
 * Returns false if short_code is NULL.
 */
bool zmk_text_expander_exists(const char *short_code);

#ifdef __cplusplus
} // End of extern "C"
#endif

#endif // ZMK_BEHAVIOR_TEXT_EXPANDER_H End of include guard.
