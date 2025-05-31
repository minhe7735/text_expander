#ifndef ZMK_TEXT_EXPANDER_INTERNALS_H // Start of include guard.
#define ZMK_TEXT_EXPANDER_INTERNALS_H

#include <zephyr/kernel.h> // For k_mutex, etc.
#include <zephyr/sys/util.h> // For ARRAY_SIZE if used, though not directly visible here.
#include <stdbool.h>       // For bool type.
#include <stdint.h>        // For uint8_t, uint16_t.

// Configuration for the maximum number of expansions that can be stored.
// Defaults to 10 if not set in Kconfig.
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS 10
#endif
// Configuration for the maximum length of a short code (excluding null terminator).
// Defaults to 16 if not set in Kconfig.
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN 16
#endif
// Configuration for the maximum length of an expanded text (excluding null terminator).
// Also used by expansion_engine.h, defined here for consistency as it impacts storage.
// Defaults to 256 if not set in Kconfig.
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN 256
#endif
// Configuration for the delay between typing characters during expansion.
// Defaults to 10 milliseconds if not set in Kconfig.
#ifndef CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY 10
#endif

// Define constants based on Kconfig or default values for easier use in code.
#define MAX_EXPANSIONS CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define MAX_SHORT_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN         // Max length for the short code string itself.
#define MAX_EXPANDED_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN   // Max length for the expanded text string.
#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY           // Milliseconds

#include <zmk/trie.h> // Include trie data structure definitions.

/**
 * @brief Structure holding the internal data for the text expander.
 *
 * This structure contains the trie used for storing expansions, the current
 * input buffer for short codes, memory pools for trie nodes and text,
 * and synchronization primitives.
 */
struct text_expander_data {
    struct trie_node *root;           // Pointer to the root of the trie storing expansions.
    char current_short[MAX_SHORT_LEN]; // Buffer to store the currently typed short code.
                                       // Its actual usable length is MAX_SHORT_LEN-1 for the null terminator.
    uint8_t current_short_len;         // Current length of the string in current_short.
    uint8_t expansion_count;           // Number of active expansions stored.
    struct k_mutex mutex;              // Mutex to protect access to shared data within this structure.

    // Memory pool for trie nodes. Sized to accommodate the maximum number of expansions,
    // where each character in a short code might potentially create a new node in the worst case.
    struct trie_node node_pool[MAX_EXPANSIONS * MAX_SHORT_LEN];
    uint16_t node_pool_used;           // Number of nodes currently allocated from node_pool.

    // Memory pool for storing the expanded text strings.
    // Sized to accommodate the maximum number of expansions, each with the maximum expanded length.
    char text_pool[MAX_EXPANSIONS * MAX_EXPANDED_LEN];
    uint16_t text_pool_used;           // Number of bytes currently allocated from text_pool.
};

/**
 * @brief Global instance of the text expander data.
 *
 * This variable is defined in text_expander.c and holds all runtime data
 * for the text expander feature.
 */
extern struct text_expander_data expander_data;

#endif // ZMK_TEXT_EXPANDER_INTERNALS_H End of include guard.
