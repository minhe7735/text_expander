#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/trie.h>
#include <zmk/text_expander_internals.h>

LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

int char_to_trie_index(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    } else if (c >= '0' && c <= '9') {
        return 26 + (c - '0');
    }
    return -1;
}

struct trie_node *trie_allocate_node(struct text_expander_data *data) {
    if (data->node_pool_used >= ARRAY_SIZE(data->node_pool)) {
        LOG_ERR("Trie node pool exhausted. Increase CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS or MAX_SHORT_LEN.");
        return NULL;
    }

    struct trie_node *node = &data->node_pool[data->node_pool_used++];
    memset(node, 0, sizeof(struct trie_node));
    return node;
}

char *trie_allocate_text_storage(struct text_expander_data *data, size_t len) {
    if (data->text_pool_used + len > sizeof(data->text_pool)) {
        LOG_ERR("Text pool exhausted. Increase CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN or MAX_EXPANSIONS.");
        return NULL;
    }

    char *text = &data->text_pool[data->text_pool_used];
    LOG_DBG("Allocated %d bytes from text pool at address %p. Pool used: %d", len, text, data->text_pool_used + len);
    data->text_pool_used += len;
    return text;
}

struct trie_node *trie_search(struct trie_node *root, const char *key) {
    if (!root || !key) {
        return NULL;
    }

    struct trie_node *current = root;

    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        int index = char_to_trie_index(c);
        if (index == -1) {
            return NULL;
        }

        if (!current->children[index]) {
            return NULL;
        }
        current = current->children[index];
    }

    return current->is_terminal ? current : NULL;
}

const char *trie_get_expanded_text(struct trie_node *node) {
    if (node && node->is_terminal) {
        return node->expanded_text;
    }
    return NULL;
}

int trie_insert(struct trie_node *root, const char *key, const char *value, struct text_expander_data *data) {
    if (!root || !key || !value) {
        return -EINVAL;
    }

    struct trie_node *current = root;

    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        int index = char_to_trie_index(c);
        if (index == -1) {
            LOG_ERR("Invalid character in short code: %c (0x%02x)", c, c);
            return -EINVAL;
        }

        if (!current->children[index]) {
            current->children[index] = trie_allocate_node(data);
            if (!current->children[index]) {
                LOG_ERR("Failed to allocate trie node for key '%s'", key);
                return -ENOMEM;
            }
        }
        current = current->children[index];
    }

    if (current->is_terminal && current->expanded_text) {
        size_t new_len = strlen(value) + 1;
        size_t old_len = strlen(current->expanded_text) + 1;

        if (new_len <= old_len) {
            strcpy(current->expanded_text, value);
            LOG_DBG("Updated existing expansion for '%s'", key);
            return 0;
        }
        LOG_WRN("New expansion for '%s' is longer, old text pool space will be orphaned.", key);
    }

    size_t text_len = strlen(value) + 1;
    current->expanded_text = trie_allocate_text_storage(data, text_len);
    if (!current->expanded_text) {
        LOG_ERR("Failed to allocate text storage for value '%s'", value);
        return -ENOMEM;
    }

    strcpy(current->expanded_text, value);
    current->is_terminal = true;
    LOG_DBG("Trie: Inserted '%s' -> '%s' at node %p, text at %p", key, current->expanded_text, current, current->expanded_text);

    return 0;
}

int trie_delete(struct trie_node *root, const char *key) {
    if (!root || !key) {
        return -EINVAL;
    }

    struct trie_node *current = root;
    struct trie_node *path[CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN];
    int path_len = 0;

    for (int i = 0; key[i] != '\0' && path_len < CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN; i++) {
        char c = key[i];
        int index = char_to_trie_index(c);
        if (index == -1) {
            return -EINVAL;
        }

        if (!current->children[index]) {
            return -ENOENT;
        }

        path[path_len++] = current;
        current = current->children[index];
    }

    if (!current->is_terminal) {
        return -ENOENT;
    }

    current->is_terminal = false;
    current->expanded_text = NULL;

    return 0;
}
