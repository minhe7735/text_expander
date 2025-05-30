/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/behavior_queue.h>

LOG_MODULE_REGISTER(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

#define MAX_EXPANSIONS CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define MAX_SHORT_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define MAX_EXPANDED_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY

// Trie node structure
struct trie_node {
    struct trie_node *children[26]; // For a-z characters
    char *expanded_text;  // NULL if not a terminal node
    bool is_terminal;     // True if this node represents end of a short code
};

struct text_expander_data {
    struct trie_node *root;
    char current_short[MAX_SHORT_LEN];
    uint8_t current_short_len;
    uint8_t expansion_count;
    struct k_mutex mutex;
    // Memory pool for trie nodes
    struct trie_node node_pool[MAX_EXPANSIONS * MAX_SHORT_LEN];
    uint16_t node_pool_used;
    // Memory pool for expanded text strings
    char text_pool[MAX_EXPANSIONS * MAX_EXPANDED_LEN];
    uint16_t text_pool_used;
};

struct text_expander_config {
    // Future configuration options can be added here
};

// Work structure for delayed execution
struct expansion_work {
    struct k_work_delayable work;
    char expanded_text[MAX_EXPANDED_LEN];
    uint8_t backspace_count;
    bool is_backspace_phase;
    size_t text_index;
};

static struct text_expander_data expander_data;
static struct expansion_work expansion_work_item;

// Flag to ensure global resources are initialized only once
static bool zmk_text_expander_global_initialized = false;

// Trie helper functions
static struct trie_node* allocate_trie_node(void) {
    if (expander_data.node_pool_used >= ARRAY_SIZE(expander_data.node_pool)) {
        LOG_ERR("Trie node pool exhausted");
        return NULL;
    }
    
    struct trie_node *node = &expander_data.node_pool[expander_data.node_pool_used++];
    memset(node, 0, sizeof(struct trie_node));
    return node;
}

static char* allocate_text_storage(size_t len) {
    if (expander_data.text_pool_used + len >= sizeof(expander_data.text_pool)) {
        LOG_ERR("Text pool exhausted");
        return NULL;
    }
    
    char *text = &expander_data.text_pool[expander_data.text_pool_used];
    expander_data.text_pool_used += len;
    return text;
}

static void free_trie_subtree(struct trie_node *node) {
    if (!node) return;
    
    // Free all children recursively
    for (int i = 0; i < 26; i++) {
        if (node->children[i]) {
            free_trie_subtree(node->children[i]);
        }
    }
    
    // Note: In this implementation, we don't actually free memory from the pools
    // as it would require complex memory management. Instead, we rely on
    // zmk_text_expander_clear_all() to reset the entire structure.
}

static struct trie_node* trie_search(struct trie_node *root, const char *key) {
    if (!root || !key) return NULL;
    
    struct trie_node *current = root;
    
    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        if (c < 'a' || c > 'z') {
            return NULL; // Invalid character
        }
        
        int index = c - 'a';
        if (!current->children[index]) {
            return NULL; // Path doesn't exist
        }
        current = current->children[index];
    }
    
    return current->is_terminal ? current : NULL;
}

static int trie_insert(struct trie_node *root, const char *key, const char *value) {
    if (!root || !key || !value) return -EINVAL;
    
    struct trie_node *current = root;
    
    // Traverse/create path for each character
    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        if (c < 'a' || c > 'z') {
            LOG_ERR("Invalid character in short code: %c", c);
            return -EINVAL;
        }
        
        int index = c - 'a';
        if (!current->children[index]) {
            current->children[index] = allocate_trie_node();
            if (!current->children[index]) {
                return -ENOMEM;
            }
        }
        current = current->children[index];
    }
    
    // If this node already has text, we're updating an existing expansion
    if (current->is_terminal && current->expanded_text) {
        // Update existing text in place if it fits
        size_t new_len = strlen(value) + 1;
        size_t old_len = strlen(current->expanded_text) + 1;
        
        if (new_len <= old_len) {
            // Can reuse existing storage
            strcpy(current->expanded_text, value);
            return 0;
        }
        // If new text is longer, we need to allocate new storage
        // Note: old storage is "leaked" but will be reclaimed on clear_all
    }
    
    // Allocate new storage for the expanded text
    size_t text_len = strlen(value) + 1;
    current->expanded_text = allocate_text_storage(text_len);
    if (!current->expanded_text) {
        return -ENOMEM;
    }
    
    strcpy(current->expanded_text, value);
    current->is_terminal = true;
    
    return 0;
}

static int trie_delete(struct trie_node *root, const char *key) {
    if (!root || !key) return -EINVAL;
    
    struct trie_node *current = root;
    struct trie_node *path[MAX_SHORT_LEN];
    int path_len = 0;
    
    // Find the node and remember the path
    for (int i = 0; key[i] != '\0' && path_len < MAX_SHORT_LEN - 1; i++) {
        char c = key[i];
        if (c < 'a' || c > 'z') {
            return -EINVAL;
        }
        
        int index = c - 'a';
        if (!current->children[index]) {
            return -ENOENT; // Key doesn't exist
        }
        
        path[path_len++] = current;
        current = current->children[index];
    }
    
    if (!current->is_terminal) {
        return -ENOENT; // Key doesn't exist
    }
    
    // Mark as non-terminal
    current->is_terminal = false;
    current->expanded_text = NULL; // Don't free, just mark as unused
    
    // Clean up nodes that are no longer needed
    // (This is a simplified cleanup - in a full implementation you'd want
    // to remove nodes that have no children and are not terminals)
    
    return 0;
}

// Public API implementation
int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text) {
    if (!short_code || !expanded_text) {
        return -EINVAL;
    }
    
    if (strlen(short_code) >= MAX_SHORT_LEN || strlen(expanded_text) >= MAX_EXPANDED_LEN) {
        return -EINVAL;
    }
    
    // Validate short_code contains only lowercase letters
    for (int i = 0; short_code[i] != '\0'; i++) {
        if (short_code[i] < 'a' || short_code[i] > 'z') {
            LOG_ERR("Short code must contain only lowercase letters: %s", short_code);
            return -EINVAL;
        }
    }
    
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    
    // Check if this is an update or new insertion
    bool is_update = (trie_search(expander_data.root, short_code) != NULL);
    
    int ret = trie_insert(expander_data.root, short_code, expanded_text);
    
    if (ret == 0) {
        if (!is_update) {
            expander_data.expansion_count++;
        }
        LOG_INF("%s expansion: %s -> %s", is_update ? "Updated" : "Added", short_code, expanded_text);
    }
    
    k_mutex_unlock(&expander_data.mutex);
    return ret;
}

int zmk_text_expander_remove_expansion(const char *short_code) {
    if (!short_code) {
        return -EINVAL;
    }
    
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    
    int ret = trie_delete(expander_data.root, short_code);
    if (ret == 0) {
        expander_data.expansion_count--;
        LOG_INF("Removed expansion: %s", short_code);
    }
    
    k_mutex_unlock(&expander_data.mutex);
    return ret;
}

void zmk_text_expander_clear_all(void) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    
    // Reset all memory pools
    memset(expander_data.node_pool, 0, sizeof(expander_data.node_pool));
    memset(expander_data.text_pool, 0, sizeof(expander_data.text_pool));
    expander_data.node_pool_used = 0;
    expander_data.text_pool_used = 0;
    expander_data.expansion_count = 0;
    
    // Reinitialize root node
    expander_data.root = allocate_trie_node();
    
    k_mutex_unlock(&expander_data.mutex);
    LOG_INF("Cleared all expansions and reset trie");
}

int zmk_text_expander_get_count(void) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    int count = expander_data.expansion_count;
    k_mutex_unlock(&expander_data.mutex);
    return count;
}

bool zmk_text_expander_exists(const char *short_code) {
    if (!short_code) {
        return false;
    }
    
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    bool exists = (trie_search(expander_data.root, short_code) != NULL);
    k_mutex_unlock(&expander_data.mutex);
    return exists;
}

// Internal functions
static const char* find_expansion(const char *short_code) {
    struct trie_node *node = trie_search(expander_data.root, short_code);
    return node ? node->expanded_text : NULL;
}

static int send_key_action(uint32_t keycode, bool pressed) {
    if (pressed) {
        return zmk_hid_keyboard_press(keycode);
    } else {
        return zmk_hid_keyboard_release(keycode);
    }
}

static int send_and_flush_key_action(uint32_t keycode, bool pressed) {
    int ret = send_key_action(keycode, pressed);
    if (ret < 0) {
        LOG_ERR("Failed to %s keycode 0x%x: %d", 
                pressed ? "press" : "release", keycode, ret);
        return ret;
    }
    
    // Send the HID report
    ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    if (ret < 0) {
        LOG_ERR("Failed to send HID report: %d", ret);
        return ret;
    }
    
    return 0;
}

static uint32_t char_to_keycode(char c, bool *needs_shift) {
    *needs_shift = false;
    
    if (c >= 'a' && c <= 'z') {
        return HID_USAGE_KEY_KEYBOARD_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *needs_shift = true;
        return HID_USAGE_KEY_KEYBOARD_A + (c - 'A');
    } else if (c >= '0' && c <= '9') {
        if (c == '0') return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
        return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (c - '1');
    }
    
    // Special characters
    switch (c) {
        case ' ': return HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        case '.': return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
        case ',': return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
        case ':': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
        case ';': return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
        case '!': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        case '@': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_2_AND_AT;
        case '#': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_3_AND_HASH;
        case '$': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR;
        case '%': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT;
        case '^': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_6_AND_CARET;
        case '&': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND;
        case '*': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK;
        case '(': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS;
        case ')': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
        case '-': return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
        case '_': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
        case '=': return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
        case '+': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
        case '\n': return HID_USAGE_KEY_KEYBOARD_RETURN_ENTER;
        case '\t': return HID_USAGE_KEY_KEYBOARD_TAB;
        case '[': return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
        case ']': return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
        case '{': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
        case '}': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
        case '\\': return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
        case '|': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE;
        case '\'': return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE;
        case '"': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE;
        case '`': return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
        case '~': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE;
        case '/': return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
        case '?': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK;
        case '<': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN;
        case '>': *needs_shift = true; return HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN;
        default: 
            LOG_WRN("Unknown character: %c (0x%02x)", c, c);
            return 0;
    }
}

static void expansion_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
    struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);
    
    if (exp_work->is_backspace_phase) {
        // Send backspace
        LOG_DBG("Sending backspace %d", exp_work->backspace_count);
        
        int ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
        if (ret < 0) return;
        
        k_msleep(TYPING_DELAY / 2);
        
        ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
        if (ret < 0) return;
        
        exp_work->backspace_count--;
        
        if (exp_work->backspace_count > 0) {
            // More backspaces to send
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
        } else {
            // Switch to text typing phase
            exp_work->is_backspace_phase = false;
            exp_work->text_index = 0;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY * 2));
        }
    } else {
        // Type text character
        if (exp_work->text_index < strlen(exp_work->expanded_text)) {
            char c = exp_work->expanded_text[exp_work->text_index];
            bool needs_shift = false;
            uint32_t keycode = char_to_keycode(c, &needs_shift);
            
            if (keycode != 0) {
                LOG_DBG("Typing character: %c (keycode: 0x%x, shift: %s)", 
                        c, keycode, needs_shift ? "yes" : "no");
                
                int ret;
                
                if (needs_shift) {
                    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true);
                    if (ret < 0) return;
                    k_msleep(TYPING_DELAY / 4);
                }
                
                ret = send_and_flush_key_action(keycode, true);
                if (ret < 0) return;
                k_msleep(TYPING_DELAY / 2);
                
                ret = send_and_flush_key_action(keycode, false);
                if (ret < 0) return;
                
                if (needs_shift) {
                    k_msleep(TYPING_DELAY / 4);
                    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
                    if (ret < 0) return;
                }
            }
            
            exp_work->text_index++;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
        } else {
            LOG_INF("Text expansion completed");
        }
    }
}

static int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len) {
    // Cancel any ongoing expansion
    k_work_cancel_delayable(&expansion_work_item.work);
    
    // Setup expansion work
    strncpy(expansion_work_item.expanded_text, expanded_text, MAX_EXPANDED_LEN - 1);
    expansion_work_item.expanded_text[MAX_EXPANDED_LEN - 1] = '\0';
    expansion_work_item.backspace_count = short_len;
    expansion_work_item.is_backspace_phase = true;
    expansion_work_item.text_index = 0;
    
    LOG_INF("Starting expansion of '%s' (len=%d) to '%s'", short_code, short_len, expanded_text);
    
    // Start with first backspace
    k_work_reschedule(&expansion_work_item.work, K_MSEC(10));
    
    return 0;
}

static void reset_current_short(void) {
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
}

static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0';
    }
}

// Behavior implementation
static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event binding_event) {
    LOG_DBG("Text expander behavior triggered");
    
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    
    if (expander_data.current_short_len > 0) {
        const char *expanded_ptr = find_expansion(expander_data.current_short);
        if (expanded_ptr) {
            char expanded_copy[MAX_EXPANDED_LEN];
            char short_copy[MAX_SHORT_LEN];
            
            // Copy strings to local buffers
            strncpy(expanded_copy, expanded_ptr, MAX_EXPANDED_LEN - 1);
            expanded_copy[MAX_EXPANDED_LEN - 1] = '\0';
            
            strncpy(short_copy, expander_data.current_short, MAX_SHORT_LEN - 1);
            short_copy[MAX_SHORT_LEN - 1] = '\0';
            
            uint8_t len_to_delete = expander_data.current_short_len;
            
            // Reset current short before unlocking
            reset_current_short();
            k_mutex_unlock(&expander_data.mutex);
            
            // Start the expansion process
            int ret = start_expansion(short_copy, expanded_copy, len_to_delete);
            if (ret < 0) {
                LOG_ERR("Failed to start expansion: %d", ret);
                return ZMK_BEHAVIOR_OPAQUE;
            }
            
            return ZMK_BEHAVIOR_OPAQUE;
        } else {
            LOG_DBG("No expansion found for '%s'", expander_data.current_short);
        }
    } else {
        LOG_DBG("No current short code to expand");
    }
    
    k_mutex_unlock(&expander_data.mutex);
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding,
                                               struct zmk_behavior_binding_event binding_event) {
    return ZMK_BEHAVIOR_TRANSPARENT;
}

// Event listener for tracking typed characters
static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    if (ev->state) { // Key pressed
        uint16_t keycode = ev->keycode;
        
        // Skip if mutex is locked (expansion in progress or API call)
        if (k_mutex_lock(&expander_data.mutex, K_NO_WAIT) != 0) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        
        // Reset on space
        if (keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
            reset_current_short();
        }
        // Add to current short for letters
        else if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
            char c = 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
            add_to_current_short(c);
        }
        // Reset on other non-modifier keys
        else if (keycode != HID_USAGE_KEY_KEYBOARD_LEFTSHIFT &&
                 keycode != HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT &&
                 keycode != HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
                 keycode != HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL &&
                 keycode != HID_USAGE_KEY_KEYBOARD_LEFTALT &&
                 keycode != HID_USAGE_KEY_KEYBOARD_RIGHTALT &&
                 keycode != HID_USAGE_KEY_KEYBOARD_LEFT_GUI &&
                 keycode != HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
            reset_current_short();
        }
        
        k_mutex_unlock(&expander_data.mutex);
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(text_expander, text_expander_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(text_expander, zmk_keycode_state_changed);

static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
};

static int text_expander_init(const struct device *dev) {
    // Initialize per-device data if needed
    struct text_expander_data *data = dev->data;
    
    if (!zmk_text_expander_global_initialized) {
        // Initialize work item
        k_work_init_delayable(&expansion_work_item.work, expansion_work_handler);
        
        k_mutex_init(&expander_data.mutex);
        
        // Initialize memory pools
        memset(&expander_data, 0, sizeof(expander_data));
        expander_data.root = allocate_trie_node();
        if (!expander_data.root) {
            LOG_ERR("Failed to allocate root trie node");
            return -ENOMEM;
        }
        
        // Initialize with some default expansions
        zmk_text_expander_add_expansion("exp", "expanded");
        zmk_text_expander_add_expansion("addr", "123 Main Street, City, State 12345");
        zmk_text_expander_add_expansion("email", "your.email@example.com");
        zmk_text_expander_add_expansion("phone", "(555) 123-4567");
        zmk_text_expander_add_expansion("sig", "Best regards,\nYour Name");
        
        LOG_INF("Text expander global resources initialized with %d default expansions", expander_data.expansion_count);
        LOG_INF("Trie structure: %d nodes allocated, %d bytes text storage used", 
                expander_data.node_pool_used, expander_data.text_pool_used);
        zmk_text_expander_global_initialized = true;
    }
    
    LOG_INF("Text expander instance initialized: %s", dev->name);
    return 0;
}

// Device tree macro to create behavior instances
#define TEXT_EXPANDER_INST(n)                                                           \
    static struct text_expander_data text_expander_data_##n;                            \
    static const struct text_expander_config text_expander_config_##n = {};             \
    BEHAVIOR_DT_INST_DEFINE(n, text_expander_init, NULL,                                \
                            &text_expander_data_##n, &text_expander_config_##n,         \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,           \
                            &text_expander_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TEXT_EXPANDER_INST)
