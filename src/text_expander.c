#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/behavior_queue.h>
#include <zmk/hid.h>

#include <zmk/text_expander.h>
#include <zmk/text_expander_internals.h>
#include <zmk/trie.h>
#include <zmk/hid_utils.h>
#include <zmk/expansion_engine.h>

LOG_MODULE_REGISTER(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

struct text_expander_data expander_data;

struct text_expander_expansion {
    const char *short_code;
    const char *expanded_text;
};

struct text_expander_config {
    const struct text_expander_expansion *expansions;
    size_t expansion_count;
};

static bool zmk_text_expander_global_initialized = false;

static const char *find_expansion(const char *short_code) {
    struct trie_node *node = trie_search(expander_data.root, short_code);
    const char *result = trie_get_expanded_text(node);

    if (result) {
        LOG_DBG("Trie search for '%s' found expansion '%s'", short_code, result);
    } else {
        LOG_DBG("Trie search for '%s' found no expansion (or node not terminal)", short_code);
    }
    return result;
}

static void reset_current_short(void) {
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
    LOG_DBG("Current short code reset.");
}

static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0';
        LOG_DBG("Current short: '%s' (len: %d)", expander_data.current_short, expander_data.current_short_len);
    } else {
        LOG_WRN("Current short code buffer full. Resetting.");
        reset_current_short();
        add_to_current_short(c);
    }
}

int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text) {
    if (!short_code || !expanded_text) {
        return -EINVAL;
    }

    size_t short_len = strlen(short_code);
    size_t expanded_len = strlen(expanded_text);

    if (short_len == 0 || short_len >= MAX_SHORT_LEN || expanded_len == 0 || expanded_len >= MAX_EXPANDED_LEN) {
        LOG_ERR("Invalid length for short code (%d) or expanded text (%d). Max short: %d, Max expanded: %d",
                short_len, expanded_len, MAX_SHORT_LEN, MAX_EXPANDED_LEN);
        return -EINVAL;
    }

    for (int i = 0; short_code[i] != '\0'; i++) {
        char c = short_code[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            LOG_ERR("Short code '%s' contains invalid character '%c'. Must be lowercase letters or numbers.", short_code, c);
            return -EINVAL;
        }
    }

    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    bool is_update = (find_expansion(short_code) != NULL);

    int ret = trie_insert(expander_data.root, short_code, expanded_text, &expander_data);

    if (ret == 0) {
        if (!is_update) {
            expander_data.expansion_count++;
        }
        LOG_INF("%s expansion: '%s' -> '%s' (Count: %d)", is_update ? "Updated" : "Added", short_code, expanded_text, expander_data.expansion_count);
    } else {
        LOG_ERR("Failed to %s expansion '%s': %d", is_update ? "update" : "add", short_code, ret);
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
        LOG_INF("Removed expansion: '%s' (Count: %d)", short_code, expander_data.expansion_count);
    } else {
        LOG_WRN("Failed to remove expansion '%s': %d (not found or invalid)", short_code, ret);
    }

    k_mutex_unlock(&expander_data.mutex);
    return ret;
}

void zmk_text_expander_clear_all(void) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    expander_data.node_pool_used = 0;
    expander_data.text_pool_used = 0;
    expander_data.expansion_count = 0;

    expander_data.root = trie_allocate_node(&expander_data);
    if (!expander_data.root) {
        LOG_ERR("Failed to re-allocate root trie node during clear operation!");
    }

    k_mutex_unlock(&expander_data.mutex);
    LOG_INF("Cleared all expansions and reset trie.");
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
    bool exists = (find_expansion(short_code) != NULL);
    k_mutex_unlock(&expander_data.mutex);
    return exists;
}

static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event binding_event) {
    LOG_DBG("Text expander behavior triggered.");

    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    if (expander_data.current_short_len > 0) {
        const char *expanded_ptr = find_expansion(expander_data.current_short);
        if (expanded_ptr) {
            char expanded_copy[MAX_EXPANDED_LEN];
            char short_copy[MAX_SHORT_LEN];

            strncpy(expanded_copy, expanded_ptr, sizeof(expanded_copy) - 1);
            expanded_copy[sizeof(expanded_copy) - 1] = '\0';
            LOG_DBG("Expanded text *after* strncpy: '%s'", expanded_copy);

            strncpy(short_copy, expander_data.current_short, sizeof(short_copy) - 1);
            short_copy[sizeof(short_copy) - 1] = '\0';

            uint8_t len_to_delete = expander_data.current_short_len;

            reset_current_short();
            k_mutex_unlock(&expander_data.mutex);

            int ret = start_expansion(short_copy, expanded_copy, len_to_delete);
            if (ret < 0) {
                LOG_ERR("Failed to start expansion: %d", ret);
                return ZMK_BEHAVIOR_OPAQUE;
            }

            return ZMK_BEHAVIOR_OPAQUE;
        } else {
            LOG_DBG("No expansion found for '%s'", expander_data.current_short);
            reset_current_short();
        }
    } else {
        LOG_DBG("No current short code to expand.");
    }

    k_mutex_unlock(&expander_data.mutex);
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding,
                                                 struct zmk_behavior_binding_event binding_event) {
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (k_mutex_lock(&expander_data.mutex, K_NO_WAIT) != 0) {
        LOG_DBG("Could not acquire mutex for keycode listener, skipping character.");
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint16_t keycode = ev->keycode;

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        char c = 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
        add_to_current_short(c);
    } else if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        char c = '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
        add_to_current_short(c);
    } else if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        add_to_current_short('0');
    } else if (keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
        reset_current_short();
    }
    else if (!(keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFTALT ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTALT ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFT_GUI ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHT_GUI ||
               keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER ||
               keycode == HID_USAGE_KEY_KEYBOARD_TAB ||
               keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE)) {
        reset_current_short();
    }

    k_mutex_unlock(&expander_data.mutex);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(text_expander, text_expander_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(text_expander, zmk_keycode_state_changed);

static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
};

static int load_expansions_from_config(const struct text_expander_config *config) {
    if (!config->expansions || config->expansion_count == 0) {
        LOG_INF("No expansions defined in device tree configuration.");
        return 0;
    }

    int loaded_count = 0;
    for (size_t i = 0; i < config->expansion_count; i++) {
        const struct text_expander_expansion *exp = &config->expansions[i];
        
        if (!exp->short_code || !exp->expanded_text) {
            LOG_WRN("Skipping invalid expansion at index %d (null short_code or expanded_text)", i);
            continue;
        }

        int ret = zmk_text_expander_add_expansion(exp->short_code, exp->expanded_text);
        if (ret == 0) {
            loaded_count++;
            LOG_DBG("Loaded expansion from DT: '%s' -> '%s'", exp->short_code, exp->expanded_text);
        } else {
            LOG_ERR("Failed to load expansion from DT: '%s' -> '%s' (error: %d)", 
                    exp->short_code, exp->expanded_text, ret);
        }
    }

    LOG_INF("Loaded %d/%d expansions from device tree configuration.", loaded_count, config->expansion_count);
    return loaded_count;
}

static int text_expander_init(const struct device *dev) {
    const struct text_expander_config *config = dev->config;

    if (!zmk_text_expander_global_initialized) {
        struct expansion_work *work_item = get_expansion_work_item();
        k_work_init_delayable(&work_item->work, expansion_work_handler);
        k_mutex_init(&expander_data.mutex);

        memset(&expander_data, 0, sizeof(expander_data));
        expander_data.root = trie_allocate_node(&expander_data);
        if (!expander_data.root) {
            LOG_ERR("Failed to allocate root trie node during initialization!");
            return -ENOMEM;
        }

        // Load expansions from device tree configuration
        int loaded_count = load_expansions_from_config(config);
        
        // Add default expansion if no expansions were loaded from DT
        if (loaded_count == 0) {
            int ret = zmk_text_expander_add_expansion("exp", "expanded");
            if (ret != 0) {
                LOG_ERR("Failed to add default expansion 'exp': %d", ret);
            } else {
                LOG_INF("Added default expansion since no DT expansions were loaded.");
            }
        }

        LOG_INF("Text expander global resources initialized with %d total expansions.", expander_data.expansion_count);
        LOG_INF("Trie structure: %d nodes used, %d bytes text storage used out of %d nodes and %d bytes total.",
                expander_data.node_pool_used, expander_data.text_pool_used,
                (int)ARRAY_SIZE(expander_data.node_pool), (int)sizeof(expander_data.text_pool));

        zmk_text_expander_global_initialized = true;
    }

    LOG_DBG("Text expander instance initialized: %s", dev->name);
    return 0;
}

// Macro to create expansions array from device tree
#define TEXT_EXPANDER_EXPANSION(node_id) \
    { \
        .short_code = DT_PROP(node_id, short_code), \
        .expanded_text = DT_PROP(node_id, expanded_text), \
    },

// Simple instance definition that always works
#define TEXT_EXPANDER_INST(n)                                                    \
    static const struct text_expander_expansion text_expander_expansions_##n[] = { \
        DT_INST_FOREACH_CHILD(n, TEXT_EXPANDER_EXPANSION)                        \
    };                                                                           \
    static const struct text_expander_config text_expander_config_##n = {       \
        .expansions = text_expander_expansions_##n,                             \
        .expansion_count = ARRAY_SIZE(text_expander_expansions_##n),            \
    };                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, text_expander_init, NULL,                        \
                            NULL, &text_expander_config_##n,                    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &text_expander_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TEXT_EXPANDER_INST)
