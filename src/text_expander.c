// Define the device driver compatibility string. This must match the 'compatible'
// property in the device tree binding YAML file for this behavior.
#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>      // For device model definitions (e.g., struct device).
#include <zephyr/kernel.h>      // For kernel objects like mutexes (k_mutex), and K_FOREVER, K_NO_WAIT.
#include <zephyr/logging/log.h> // For Zephyr's logging API.
#include <zephyr/sys/util.h>    // Required for IS_ENABLED and ARRAY_SIZE macros.
#include <drivers/behavior.h>   // For behavior driver API structures and return codes (e.g. ZMK_BEHAVIOR_OPAQUE).
#include <errno.h>              // Required for error codes like EINVAL, ENOENT, ENOMEM.

#include <zmk/behavior.h>             // ZMK core behavior system.
#include <zmk/event_manager.h>        // ZMK event manager for subscribing to events.
#include <zmk/events/keycode_state_changed.h> // Event type for key presses/releases.
#include <zmk/keymap.h>               // For keymap related utilities (not directly used but often related).
#include <zmk/behavior_queue.h>       // For behavior queue interaction (not directly used here).
#include <zmk/hid.h>                  // For HID usage page definitions (e.g. HID_USAGE_KEY_KEYBOARD_A).

#include <zmk/text_expander.h>          // Public API for text expander functions.
#include <zmk/text_expander_internals.h> // Internal data structures and constants (expander_data, MAX_SHORT_LEN etc.).
#include <zmk/trie.h>                   // Trie data structure for storing and searching expansions.
#include <zmk/hid_utils.h>              // Utilities for converting chars to keycodes and sending HID reports.
#include <zmk/expansion_engine.h>       // Engine for handling the typing of expanded text.

// Register a logging module for this file.
LOG_MODULE_REGISTER(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

// Definition of the global text_expander_data structure. This was declared as 'extern'
// in text_expander_internals.h. It holds all runtime state for the expander.
struct text_expander_data expander_data;

// Structure used to define an expansion read from the device tree.
struct text_expander_expansion {
    const char *short_code;    // The short code string.
    const char *expanded_text; // The corresponding expanded text string.
};

// Structure to hold the configuration for a text expander device instance,
// primarily a list of expansions loaded from the device tree.
struct text_expander_config {
    const struct text_expander_expansion *expansions; // Pointer to an array of expansions.
    size_t expansion_count;                           // Number of expansions in the array.
};

// Flag to ensure global resources (like the trie root and memory pools within expander_data)
// are initialized only once, even if multiple text_expander behavior instances are defined
// in the device tree (though typically there's one logical expander).
static bool zmk_text_expander_global_initialized = false;

/**
 * @brief Searches for an expansion for the given short_code in the trie.
 *
 * @param short_code The short code to look up.
 * @return A pointer to the expanded text if found and the node is terminal, otherwise NULL.
 */
static const char *find_expansion(const char *short_code) {
    // Search the trie for the given short code.
    struct trie_node *node = trie_search(expander_data.root, short_code);
    // Get the expanded text from the node (returns NULL if node is NULL or not terminal).
    const char *result = trie_get_expanded_text(node);

    if (result) {
        LOG_DBG("Trie search for '%s' found expansion '%s'", short_code, result);
    } else {
        LOG_DBG("Trie search for '%s' found no expansion (or node not terminal)", short_code);
    }
    return result;
}

/**
 * @brief Resets the current short code buffer (expander_data.current_short).
 * Clears the buffer and resets its length to 0.
 */
static void reset_current_short(void) {
    memset(expander_data.current_short, 0, MAX_SHORT_LEN); // Fill buffer with zeros.
    expander_data.current_short_len = 0;                   // Reset length.
    LOG_DBG("Current short code reset.");
}

/**
 * @brief Appends a character to the current short code buffer.
 *
 * If the buffer is full, it logs a warning and resets the buffer before attempting
 * to add the character (though in this version, the character that caused the overflow is lost
 * after reset, unless the commented-out lines are re-enabled).
 *
 * @param c The character to add.
 */
static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) { // Check space for char + null terminator.
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0'; // Ensure null termination.
        LOG_DBG("Current short: '%s' (len: %d)", expander_data.current_short, expander_data.current_short_len);
    } else {
        // Buffer is full. Log the current content before resetting.
        char temp_buffer[MAX_SHORT_LEN];
        strncpy(temp_buffer, expander_data.current_short, MAX_SHORT_LEN -1);
        temp_buffer[MAX_SHORT_LEN -1] = '\0'; // Ensure null termination for logging.

        LOG_WRN("Current short code buffer full ('%s', len %d). Resetting before adding '%c'. Max len: %d",
                temp_buffer, expander_data.current_short_len, c, MAX_SHORT_LEN -1);
        reset_current_short();
        // Original behavior: the char 'c' that caused overflow is lost.
        // To add it after reset:
        // expander_data.current_short[expander_data.current_short_len++] = c;
        // expander_data.current_short[expander_data.current_short_len] = '\0';
        // LOG_DBG("Current short after reset and add: '%s' (len: %d)", expander_data.current_short, expander_data.current_short_len);
    }
}

/**
 * @brief Public API function to add or update a text expansion.
 * (Implementation of the function declared in zmk_text_expander.h)
 */
int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text) {
    if (!short_code || !expanded_text) { // Null checks.
        return -EINVAL; // Invalid argument.
    }

    size_t short_len = strlen(short_code);
    size_t expanded_len = strlen(expanded_text);

    // Validate lengths against configured maximums.
    if (short_len == 0 || short_len >= MAX_SHORT_LEN || 
        expanded_len == 0 || expanded_len >= MAX_EXPANDED_LEN) {
        LOG_ERR("Invalid length for short code (%zu) or expanded text (%zu). Max short: %d, Max expanded: %d",
                short_len, expanded_len, MAX_SHORT_LEN, MAX_EXPANDED_LEN);
        return -EINVAL;
    }

    // Validate characters in the short code (must be lowercase alphanumeric).
    for (int i = 0; short_code[i] != '\0'; i++) {
        char c = short_code[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            LOG_ERR("Short code '%s' contains invalid character '%c'. Must be lowercase letters or numbers.", short_code, c);
            return -EINVAL;
        }
    }

    k_mutex_lock(&expander_data.mutex, K_FOREVER); // Acquire mutex for thread-safe access.

    // Check if this short_code already exists (to log as "Updated" vs "Added").
    bool is_update = (find_expansion(short_code) != NULL);

    // Insert the expansion into the trie.
    int ret = trie_insert(expander_data.root, short_code, expanded_text, &expander_data);

    if (ret == 0) { // Success.
        if (!is_update) {
            expander_data.expansion_count++; // Increment count only for new additions.
        }
        LOG_INF("%s expansion: '%s' -> '%s' (Count: %d)", 
                is_update ? "Updated" : "Added", short_code, expanded_text, expander_data.expansion_count);
    } else {
        LOG_ERR("Failed to %s expansion '%s': %d", is_update ? "update" : "add", short_code, ret);
    }

    k_mutex_unlock(&expander_data.mutex); // Release mutex.
    return ret;
}

/**
 * @brief Public API function to remove a text expansion.
 * (Implementation of the function declared in zmk_text_expander.h)
 */
int zmk_text_expander_remove_expansion(const char *short_code) {
    if (!short_code) {
        return -EINVAL;
    }

    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    // Attempt to delete from the trie.
    // trie_delete marks the node as non-terminal but doesn't free memory pools here.
    int ret = trie_delete(expander_data.root, short_code); 
    if (ret == 0) { // Successfully found and "deleted" (marked non-terminal).
        expander_data.expansion_count--;
        LOG_INF("Removed expansion: '%s' (Count: %d)", short_code, expander_data.expansion_count);
    } else if (ret == -ENOENT) { // Entry not found.
        LOG_WRN("Failed to remove expansion '%s': Not found.", short_code);
    } else { // Other error during deletion.
        LOG_WRN("Failed to remove expansion '%s': Error %d", short_code, ret);
    }

    k_mutex_unlock(&expander_data.mutex);
    return ret;
}

/**
 * @brief Public API function to clear all text expansions.
 * (Implementation of the function declared in zmk_text_expander.h)
 */
void zmk_text_expander_clear_all(void) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    // Reset memory pool usage counters. This effectively "frees" all pooled memory
    // for nodes and text, making it available for new allocations.
    expander_data.node_pool_used = 0;
    expander_data.text_pool_used = 0;
    expander_data.expansion_count = 0;
    
    // Reset the current short code input buffer as well.
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;

    // Re-initialize the trie by allocating a new root node.
    // This will use the first node from the (now considered empty) node_pool.
    expander_data.root = trie_allocate_node(&expander_data); 
    if (!expander_data.root) {
        // This is a critical failure, as the trie cannot operate without a root.
        LOG_ERR("Failed to re-allocate root trie node during clear operation!");
        // The system might be in an unstable state if this happens.
    }

    k_mutex_unlock(&expander_data.mutex);
    LOG_INF("Cleared all expansions and reset trie.");
}

/**
 * @brief Public API function to get the count of current expansions.
 * (Implementation of the function declared in zmk_text_expander.h)
 */
int zmk_text_expander_get_count(void) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    int count = expander_data.expansion_count;
    k_mutex_unlock(&expander_data.mutex);
    return count;
}

/**
 * @brief Public API function to check if an expansion exists.
 * (Implementation of the function declared in zmk_text_expander.h)
 */
bool zmk_text_expander_exists(const char *short_code) {
    if (!short_code) {
        return false;
    }

    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    bool exists = (find_expansion(short_code) != NULL);
    k_mutex_unlock(&expander_data.mutex);
    return exists;
}


/**
 * @brief Event listener for keycode state changes (key presses/releases).
 *
 * This function is called by the ZMK event manager whenever a keycode_state_changed event occurs.
 * It processes key presses to:
 * 1. Build the `current_short` code buffer from alphanumeric keys.
 * 2. Handle Backspace to edit the `current_short` buffer.
 * 3. Implement aggressive reset mode: if typed characters do not form a prefix of any
 * known short code, the buffer is reset.
 * 4. Handle specific keys (like Space, or others based on Kconfig) that should
 * reset the `current_short` buffer.
 *
 * @param eh Pointer to the generic zmk_event_t.
 * @return ZMK_EV_EVENT_BUBBLE to allow other listeners to process the event,
 * or ZMK_EV_EVENT_CONSUME if the event should be stopped here (not used in this impl).
 */
static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    // Cast the generic event to the specific keycode_state_changed event type.
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    // Only process key presses (ev->state is true for press, false for release).
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE; // Let other listeners handle releases or null events.
    }

    // Attempt to lock the mutex without waiting. If busy, skip this key press to avoid blocking
    // the event handling thread. This is a trade-off: might miss a char if system is heavily loaded.
    if (k_mutex_lock(&expander_data.mutex, K_NO_WAIT) != 0) {
        LOG_DBG("Could not acquire mutex for keycode listener, skipping character.");
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint16_t keycode = ev->keycode; // The HID keycode from the event.
    bool current_short_content_changed = false; // Flag to track if current_short buffer was modified.

    // --- 1. Handle keys that modify current_short (alphanumeric, backspace) ---
    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        char c = 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A); // Convert keycode to lowercase char.
        add_to_current_short(c);
        current_short_content_changed = true;
    } else if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && 
               keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        char c = '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION); // Convert num-row keycode to digit.
        add_to_current_short(c);
        current_short_content_changed = true;
    } else if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        add_to_current_short('0'); // Handle '0' key.
        current_short_content_changed = true;
    } else if (keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        if (expander_data.current_short_len > 0) { // If buffer is not empty.
            expander_data.current_short_len--;     // "Delete" last char by reducing length.
            expander_data.current_short[expander_data.current_short_len] = '\0'; // Null-terminate.
            LOG_DBG("Backspace. Current short: '%s', len: %d",
                    expander_data.current_short, expander_data.current_short_len);
            current_short_content_changed = true;
        }
    }

    // --- 2. Aggressive reset logic (if enabled and short code content changed) ---
    // If "aggressive reset mode" is enabled via Kconfig:
    // Check if the current_short (after modification) is still a valid prefix in the trie.
    // If not, reset current_short. This prevents long, invalid sequences from accumulating.
    if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE)) {
        if (current_short_content_changed && expander_data.current_short_len > 0) {
            // trie_get_node_for_key returns NULL if the key is not a valid path/prefix.
            struct trie_node *node = trie_get_node_for_key(expander_data.root, expander_data.current_short);
            if (node == NULL) {
                LOG_DBG("Aggressive reset: '%s' is not a prefix of any known short code. Resetting.",
                        expander_data.current_short);
                reset_current_short();
                current_short_content_changed = false; // Buffer is now reset, so effectively no "new" content from this key event.
            }
        }
    }

    // --- 3. Handle specific reset keys (Space) or other generic non-alphanumeric keys ---
    if (keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
        // Spacebar always resets the current_short if it's not empty.
        // This is a common trigger for users to indicate the end of a potential short code
        // if it wasn't a recognized one to be expanded by the behavior key.
        if (expander_data.current_short_len > 0) {
            reset_current_short();
        }
    } else if (
        // This block handles other keys that should generally reset the short_code buffer.
        // It executes if:
        //   - The current_short content was NOT changed by this key event (i.e., it wasn't an alphanumeric/backspace)
        //     OR if aggressive reset already cleared it.
        !current_short_content_changed &&
        //   - AND the key is NOT one of the keys involved in building short codes (alphanum, backspace)
        //   - AND the key is NOT Space (already handled)
        //   - AND the key is NOT a common modifier key (Shift, Ctrl, Alt, GUI)
        //   - AND the key is NOT Enter or Tab, IF Kconfig options specify they should NOT reset.
        !( 
            (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) ||
            (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && keycode <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ||
            keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE ||
            keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR || // Already handled explicitly
            // Modifiers (these should not reset the buffer)
            keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT ||
            keycode == HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT ||
            keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL ||
            keycode == HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL ||
            keycode == HID_USAGE_KEY_KEYBOARD_LEFTALT ||
            keycode == HID_USAGE_KEY_KEYBOARD_RIGHTALT ||
            keycode == HID_USAGE_KEY_KEYBOARD_LEFT_GUI ||
            keycode == HID_USAGE_KEY_KEYBOARD_RIGHT_GUI ||
            // Conditionally non-resetting Enter (if Kconfig ZMK_TEXT_EXPANDER_RESET_ON_ENTER is false)
            (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER) && keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) ||
            // Conditionally non-resetting Tab (if Kconfig ZMK_TEXT_EXPANDER_RESET_ON_TAB is false)
            (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_TAB) && keycode == HID_USAGE_KEY_KEYBOARD_TAB)
        )
    ) {
        // If all conditions above are met, this key is considered a "generic" key that
        // should terminate the current short code attempt.
        if (expander_data.current_short_len > 0) { // Only reset if buffer is not already empty.
            LOG_DBG("Generic reset for key 0x%02X. Resetting current short '%s'.", keycode, expander_data.current_short);
            reset_current_short();
        }
    }

    k_mutex_unlock(&expander_data.mutex); // Release the mutex.
    return ZMK_EV_EVENT_BUBBLE; // Allow other event listeners to process this key event.
}


/**
 * @brief Behavior action called when the key assigned to this behavior is pressed.
 *
 * This function attempts to find an expansion for the `current_short` code.
 * If found, it initiates the expansion process (backspacing the short code, then typing
 * the expanded text). If not found, or if `current_short` is empty, it resets `current_short`.
 *
 * @param binding Pointer to the behavior binding data.
 * @param binding_event Event data for the binding.
 * @return ZMK_BEHAVIOR_OPAQUE if an expansion was attempted (consumes the event).
 * @return ZMK_BEHAVIOR_TRANSPARENT if no action was taken (e.g., current_short was empty).
 */
static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event binding_event) {
    LOG_DBG("Text expander behavior &%s triggered.", binding->behavior_dev);

    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    if (expander_data.current_short_len > 0) { // If there's something in the short code buffer.
        // Try to find an expansion for the current short code.
        const char *expanded_ptr = find_expansion(expander_data.current_short);
        
        if (expanded_ptr) { // Expansion found!
            // Make copies of the short code and expanded text. This is important because:
            // 1. reset_current_short() will clear expander_data.current_short.
            // 2. The expansion engine operates asynchronously, so it needs its own copy
            //    of the expanded text. The expanded_ptr points into the shared text_pool
            //    which could theoretically change if another operation modified expansions
            //    (though less likely during an active expansion). A copy is safer.
            char expanded_copy[MAX_EXPANDED_LEN]; 
            char short_copy[MAX_SHORT_LEN];     

            strncpy(expanded_copy, expanded_ptr, sizeof(expanded_copy) - 1);
            expanded_copy[sizeof(expanded_copy) - 1] = '\0'; // Ensure null termination.

            strncpy(short_copy, expander_data.current_short, sizeof(short_copy) - 1);
            short_copy[sizeof(short_copy) - 1] = '\0'; // Ensure null termination.

            uint8_t len_to_delete = expander_data.current_short_len; // Store length before reset.

            reset_current_short(); // Reset the buffer immediately after deciding to expand.
            k_mutex_unlock(&expander_data.mutex); // Unlock BEFORE starting the expansion engine,
                                                  // as the engine itself might need to log or interact
                                                  // with systems that could try to acquire this mutex later.

            LOG_DBG("Attempting to expand '%s' to '%s' (delete %d chars)", short_copy, expanded_copy, len_to_delete);
            // Start the asynchronous expansion process.
            int ret = start_expansion(short_copy, expanded_copy, len_to_delete);
            if (ret < 0) {
                LOG_ERR("Failed to start expansion: %d", ret);
                // Mutex is already unlocked.
                // Even on failure to start, we consider the event "handled" (opaque)
                // because an action related to the behavior was attempted.
                return ZMK_BEHAVIOR_OPAQUE;
            }
            return ZMK_BEHAVIOR_OPAQUE; // Expansion started, consume the event.
        } else {
            // No expansion found for the current short code.
            LOG_DBG("No expansion found for '%s'. Resetting short code.", expander_data.current_short);
            reset_current_short(); // Reset the buffer.
        }
    } else {
        // current_short buffer was empty, nothing to expand.
        LOG_DBG("No current short code to expand.");
    }

    k_mutex_unlock(&expander_data.mutex);
    // If no expansion was found or buffer was empty, the behavior key press doesn't "do" anything
    // other than potentially reset an invalid short code (which is a side effect).
    // Treat as transparent so the underlying key (if any) can act.
    return ZMK_BEHAVIOR_TRANSPARENT; 
}

/**
 * @brief Behavior action called when the key assigned to this behavior is released.
 *
 * This behavior typically doesn't do anything on key release.
 *
 * @param binding Pointer to the behavior binding data.
 * @param binding_event Event data for the binding.
 * @return ZMK_BEHAVIOR_TRANSPARENT.
 */
static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding,
                                                 struct zmk_behavior_binding_event binding_event) {
    // No action on release for this behavior.
    return ZMK_BEHAVIOR_TRANSPARENT;
}


// ZMK Listener definition: Connects the text_expander_keycode_state_changed_listener function
// to the ZMK event system.
ZMK_LISTENER(text_expander_listener_interface, text_expander_keycode_state_changed_listener);
// ZMK Subscription: Subscribes the defined listener to zmk_keycode_state_changed events.
// This ensures our listener is called whenever such an event is published.
ZMK_SUBSCRIPTION(text_expander_listener_interface, zmk_keycode_state_changed);


// Define the driver API structure for this behavior.
// This structure provides pointers to the functions that handle behavior events (press, release).
static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
    // Other API function pointers like .init, .config, etc., would go here if needed.
};

/**
 * @brief Loads text expansions from the device tree configuration.
 *
 * Iterates through child nodes of the text expander behavior node in the DTS,
 * extracting `short_code` and `expanded_text` properties and adding them
 * as expansions.
 *
 * @param config Pointer to the text_expander_config for this device instance,
 * containing the array of expansions from DTS.
 * @return The number of expansions successfully loaded.
 */
static int load_expansions_from_config(const struct text_expander_config *config) {
    if (!config || !config->expansions || config->expansion_count == 0) {
        LOG_INF("No expansions defined in device tree configuration.");
        return 0; // No configuration provided or no expansions listed.
    }

    int loaded_count = 0;
    for (size_t i = 0; i < config->expansion_count; i++) {
        const struct text_expander_expansion *exp = &config->expansions[i]; // Get current expansion from array.
        
        // Basic validation: ensure pointers are not null.
        if (!exp->short_code || !exp->expanded_text) {
            LOG_WRN("Skipping invalid expansion at index %zu (null short_code or expanded_text)", i);
            continue;
        }
        // Defensive check for empty strings, although zmk_text_expander_add_expansion also checks this.
        // An empty short_code or expanded_text is usually not intended.
        if (exp->short_code[0] == '\0' || exp->expanded_text[0] == '\0') {
            LOG_WRN("Skipping expansion with empty short_code or expanded_text at index %zu", i);
            continue;
        }

        // Add the expansion using the public API function.
        // This ensures all validation (length, characters) and trie insertion logic is applied.
        int ret = zmk_text_expander_add_expansion(exp->short_code, exp->expanded_text);
        if (ret == 0) { // Success.
            loaded_count++;
            LOG_DBG("Loaded expansion from DT: '%s' -> '%s'", exp->short_code, exp->expanded_text);
        } else { // Failed to add.
            LOG_ERR("Failed to load expansion from DT: '%s' -> '%s' (error: %d)", 
                    exp->short_code, exp->expanded_text, ret);
        }
    }

    LOG_INF("Loaded %d/%zu expansions from device tree configuration.", loaded_count, config->expansion_count);
    return loaded_count;
}

/**
 * @brief Initialization function for the text expander behavior device.
 *
 * This function is called by Zephyr when the device driver for this behavior is initialized.
 * It performs one-time global initialization for the text expander system (like setting up
 * the trie, mutex, and memory pools) and then loads any expansions defined in the
 * device tree for this specific instance.
 *
 * @param dev Pointer to the device structure for this behavior instance.
 * @return 0 on success, or a negative error code if initialization fails.
 */
static int text_expander_init(const struct device *dev) {
    // Get the configuration data for this device instance (contains DTS expansions).
    const struct text_expander_config *config = dev->config;

    // --- Global Initialization (once per system) ---
    if (!zmk_text_expander_global_initialized) {
        k_mutex_init(&expander_data.mutex); // Initialize the mutex first.

        // Initialize memory pool usage counters and other global data.
        expander_data.node_pool_used = 0;
        expander_data.text_pool_used = 0;
        expander_data.expansion_count = 0;
        memset(expander_data.current_short, 0, MAX_SHORT_LEN); // Clear current short buffer.
        expander_data.current_short_len = 0;

        // Allocate the root node for the trie from our pool.
        expander_data.root = trie_allocate_node(&expander_data);
        if (!expander_data.root) {
            LOG_ERR("Failed to allocate root trie node during initialization!");
            return -ENOMEM; // Cannot proceed without a trie root.
        }
        
        // Initialize the delayable work item for the expansion engine.
        // get_expansion_work_item() returns a pointer to the static work item in expansion_engine.c.
        struct expansion_work *work_item = get_expansion_work_item(); 
        if (work_item) {
            // expansion_work_handler is the function that will be called when the work is processed.
            k_work_init_delayable(&work_item->work, expansion_work_handler);
        } else {
            // This should ideally not happen if get_expansion_work_item always returns a valid static item.
            LOG_ERR("Failed to get expansion work item for initialization!");
            // Depending on how critical this is, could return an error.
        }

        // Load expansions defined in the device tree for THIS instance.
        // Note: If multiple DT nodes use this behavior, this load_expansions_from_config
        // will be called for each, but they all add to the same global trie.
        int loaded_count = load_expansions_from_config(config);
        
        // Optional: Add a default expansion if no expansions were loaded from DT.
        // This can be useful for testing or providing a basic example.
        // Check both loaded_count (from current DT instance) and expander_data.expansion_count (total in trie).
        if (loaded_count == 0 && expander_data.expansion_count == 0) { 
            LOG_INF("No expansions loaded from any DT source. Adding default 'exp' -> 'expanded'.");
            int ret = zmk_text_expander_add_expansion("exp", "expanded");
            if (ret != 0) {
                LOG_ERR("Failed to add default expansion 'exp': %d", ret);
            }
        }

        LOG_INF("Text expander global resources initialized. Total expansions currently: %d.", expander_data.expansion_count);
        LOG_INF("Trie memory usage: %d nodes used (out of %zu max pool size), %d bytes for text storage (out of %zu max pool size).",
                expander_data.node_pool_used, ARRAY_SIZE(expander_data.node_pool), // ARRAY_SIZE gives total elements in pool
                expander_data.text_pool_used, sizeof(expander_data.text_pool));   // sizeof gives total bytes in pool

        zmk_text_expander_global_initialized = true; // Mark global init as complete.
    } else {
        // If global init was already done by another instance, we still might want to load
        // expansions specific to *this* device tree instance if the design supported per-instance
        // configurations that add to the global set.
        // The current `load_expansions_from_config(config)` call here will attempt to load
        // expansions from the current `dev->config`.
        LOG_DBG("Text expander global resources already initialized. Processing config for instance: %s", dev->name);
        load_expansions_from_config(config); // Load expansions for this specific instance.
                                             // They will be added to the already initialized global trie.
        LOG_INF("After processing instance %s, total expansions: %d.", dev->name, expander_data.expansion_count);

    }

    LOG_DBG("Text expander instance initialized: %s (driver %p, config %p, data %p)", 
            dev->name, dev->api, dev->config, dev->data);
    return 0; // Success.
}

// --- Device Tree Instance Creation Macros ---

// Macro to create a single `struct text_expander_expansion` initializer
// from a device tree child node (e.g., a specific expansion like `myemail: short_code = "eml", expanded_text = "...";`).
// DT_PROP_OR gets the property value or a default if not present.
#define TEXT_EXPANDER_EXPANSION(node_id) \
    { \
        .short_code = DT_PROP_OR(node_id, short_code, ""), \
        .expanded_text = DT_PROP_OR(node_id, expanded_text, ""), \
    },

// Macro to define a text expander behavior device instance.
// This is used by DT_INST_FOREACH_STATUS_OKAY to create C structures and
// register the driver for each enabled instance in the device tree.
// `n` is the instance number.
#define TEXT_EXPANDER_INST(n)                                                    \
    /* Create a static array of text_expander_expansion structures for this instance, */ \
    /* populated from its child nodes in the device tree. */                       \
    static const struct text_expander_expansion text_expander_expansions_##n[] = { \
        DT_INST_FOREACH_CHILD(n, TEXT_EXPANDER_EXPANSION) /* Iterate over child nodes of instance 'n' */ \
    };                                                                           \
    /* Create the configuration structure for this instance, pointing to the array above. */ \
    static const struct text_expander_config text_expander_config_##n = {       \
        .expansions = text_expander_expansions_##n,                             \
        .expansion_count = ARRAY_SIZE(text_expander_expansions_##n), /* Number of expansions for this instance */ \
    };                                                                          \
    /* Define and register the behavior device instance using ZMK's BEHAVIOR_DT_INST_DEFINE. */ \
    /* - text_expander_init: Initialization function. */                         \
    /* - NULL: No power management function. */                                  \
    /* - &expander_data: Pointer to the (shared) runtime data structure. */      \
    /* - &text_expander_config_##n: Pointer to this instance's configuration. */ \
    /* - POST_KERNEL: Initialization level. */                                   \
    /* - CONFIG_KERNEL_INIT_PRIORITY_DEFAULT: Initialization priority. */        \
    /* - &text_expander_driver_api: Pointer to the behavior's driver API. */     \
    BEHAVIOR_DT_INST_DEFINE(n, text_expander_init, NULL,                        \
                            &expander_data, &text_expander_config_##n,          \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &text_expander_driver_api);

// This Zephyr macro iterates over all device tree instances that have a status of "okay"
// (i.e., are enabled) and match the `DT_DRV_COMPAT` string defined at the top of this file.
// For each such instance, it invokes the `TEXT_EXPANDER_INST` macro with the instance number.
DT_INST_FOREACH_STATUS_OKAY(TEXT_EXPANDER_INST)
