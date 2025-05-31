#include <zephyr/kernel.h>      // For basic Zephyr types, not strictly essential here but common.
#include <zephyr/logging/log.h> // For Zephyr's logging API (LOG_ERR, LOG_DBG, LOG_WRN).
#include <string.h>             // For memset, strcpy, strlen.
#include <errno.h>              // For error codes like EINVAL (invalid argument), 
                                // ENOMEM (no memory), ENOENT (no such entry).

#include <zmk/trie.h>                   // Public API for the trie.
#include <zmk/text_expander_internals.h> // For text_expander_data structure definition, which contains
                                        // the memory pools (node_pool, text_pool) and their usage counters,
                                        // and MAX_SHORT_LEN.

// Define a logging module for this file, consistent with other text_expander files.
LOG_MODULE_DECLARE(zmk_behavior_text_expander, CONFIG_ZMK_LOG_LEVEL);

/**
 * @brief Converts a character to its corresponding index in the trie's children array.
 *
 * Maps lowercase letters 'a'-'z' to indices 0-25.
 * Maps digits '0'-'9' to indices 26-35.
 * This defines the alphabet supported by the trie.
 *
 * @param c The character to convert.
 * @return The calculated index (0-35) if the character is valid for the trie,
 * -1 otherwise.
 */
int char_to_trie_index(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';         // 'a' -> 0, 'b' -> 1, ..., 'z' -> 25
    } else if (c >= '0' && c <= '9') {
        return 26 + (c - '0');  // '0' -> 26, '1' -> 27, ..., '9' -> 35
    }
    return -1; // Character is not in the supported alphabet.
}

/**
 * @brief Allocates a new trie node from the pre-allocated node_pool.
 *
 * The node_pool is part of the `text_expander_data` structure. This function
 * increments `data->node_pool_used` to claim the next available node.
 * The allocated node is zero-initialized.
 *
 * @param data Pointer to the `text_expander_data` structure containing the node pool.
 * @return Pointer to the newly allocated `trie_node`, or NULL if the pool is exhausted.
 */
struct trie_node *trie_allocate_node(struct text_expander_data *data) {
    // Check if the node pool has space for another node.
    // ARRAY_SIZE is a Zephyr utility to get the number of elements in an array.
    if (data->node_pool_used >= ARRAY_SIZE(data->node_pool)) {
        LOG_ERR("Trie node pool exhausted. Current usage: %u, Max: %zu. Increase CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS or CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN.",
                data->node_pool_used, ARRAY_SIZE(data->node_pool));
        return NULL; // No space left in the pool.
    }

    // Get the address of the next available node in the pool.
    struct trie_node *node = &data->node_pool[data->node_pool_used++];
    // Initialize the allocated node's memory to zero.
    // This sets all child pointers to NULL and boolean flags (like is_terminal) to false.
    memset(node, 0, sizeof(struct trie_node));
    return node;
}

/**
 * @brief Allocates a block of memory for storing expanded text from the pre-allocated text_pool.
 *
 * The text_pool is part of the `text_expander_data` structure. This function
 * increments `data->text_pool_used` by `len` to claim the next available block.
 *
 * @param data Pointer to the `text_expander_data` structure containing the text pool.
 * @param len The number of bytes to allocate (should include space for null terminator).
 * @return Pointer to the start of the allocated block in `text_pool`, or NULL if the pool
 * does not have enough contiguous space.
 */
char *trie_allocate_text_storage(struct text_expander_data *data, size_t len) {
    // Check if the text pool has enough remaining space for the requested length.
    // sizeof(data->text_pool) gives the total size of the text_pool array in bytes.
    if (data->text_pool_used + len > sizeof(data->text_pool)) {
        LOG_ERR("Text pool exhausted. Requested: %zu, Used: %u, Total: %zu. Increase CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN or CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS.",
                len, data->text_pool_used, sizeof(data->text_pool));
        return NULL; // Not enough space.
    }

    // Get the address of the start of the allocatable block in the pool.
    char *text = &data->text_pool[data->text_pool_used];
    LOG_DBG("Allocated %zu bytes from text pool at address %p. Pool used will be: %u", 
            len, (void*)text, data->text_pool_used + (uint16_t)len); // Cast len for consistent type in log if needed
    // Advance the used counter by the allocated length.
    data->text_pool_used += (uint16_t)len; // Assuming len will fit in uint16_t text_pool_used capacity
    return text;
}

/**
 * @brief Searches the trie for a given key (short code).
 *
 * Traverses the trie based on the characters in the key.
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string to search for.
 * @return Pointer to the `trie_node` if the full key is found AND it's marked as a terminal node.
 * Returns NULL if the key is not found, if the key contains invalid characters,
 * or if the key path exists but is not a terminal node.
 */
struct trie_node *trie_search(struct trie_node *root, const char *key) {
    if (!root || !key) { // Basic null checks for robustness.
        return NULL;
    }

    struct trie_node *current = root; // Start traversal from the root.

    // Iterate through each character of the key.
    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        int index = char_to_trie_index(c); // Convert character to trie alphabet index.
        
        if (index == -1) { // Invalid character in key.
            return NULL;
        }

        if (!current->children[index]) { // No child node for this character, so key doesn't exist.
            return NULL;
        }
        current = current->children[index]; // Move to the child node.
    }

    // After processing all characters in the key, check if the final node is terminal.
    // Only terminal nodes represent complete, stored short codes.
    return current->is_terminal ? current : NULL;
}

/**
 * @brief Traverses the trie for the given key to find the node corresponding to the end of the key.
 *
 * This function is used to check if a string `key` forms a valid path (prefix)
 * within the trie. Unlike `trie_search`, it does NOT require the node to be terminal.
 *
 * @param root The root node of the trie.
 * @param key The null-terminated key string (can be a prefix) to search for.
 * @return Pointer to the `trie_node` at the end of the key path if it exists.
 * Returns `NULL` if `root` or `key` is `NULL`, if `key` contains invalid characters,
 * or if the path for `key` does not exist in the trie.
 * If `key` is an empty string, returns `root`.
 */
struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
    if (!root || !key) {
        return NULL;
    }

    struct trie_node *current = root;

    // An empty key is considered a valid prefix, and its "node" is the root itself.
    if (key[0] == '\0') {
        return root;
    }

    // Iterate through each character of the key.
    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        int index = char_to_trie_index(c); // Convert character to trie alphabet index.
        
        if (index == -1) { // Invalid character in key, so it cannot be a prefix.
            return NULL;
        }

        if (!current->children[index]) { // No child node for this character, path doesn't exist.
            return NULL;
        }
        current = current->children[index]; // Move to the child node.
    }

    return current; // Path exists, return the node at the end of the key.
}


/**
 * @brief Retrieves the expanded text associated with a trie node.
 *
 * @param node Pointer to a `trie_node`.
 * @return A const pointer to the null-terminated expanded text string if `node` is
 * not NULL and `node->is_terminal` is true. Otherwise, returns NULL.
 */
const char *trie_get_expanded_text(struct trie_node *node) {
    if (node && node->is_terminal) {
        return node->expanded_text;
    }
    return NULL;
}

/**
 * @brief Inserts a key-value pair (short code and its expansion) into the trie.
 *
 * If the key already exists and is terminal:
 * - If the new value fits into the previously allocated space for the old value,
 * the old value is overwritten.
 * - If the new value is longer, new space is allocated, and the old space is orphaned
 * (not immediately reclaimed, but will be "freed" if `zmk_text_expander_clear_all` is called).
 * If the key path exists but the node wasn't terminal, it's marked terminal and value stored.
 * If the key path doesn't fully exist, new nodes are allocated as needed.
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string (must be lowercase alphanumeric).
 * @param value The null-terminated expanded text string.
 * @param data Pointer to `text_expander_data` for memory allocation from pools.
 * @return 0 on success.
 * @return -EINVAL if `root`, `key`, or `value` is NULL, or if `key` contains invalid characters.
 * @return -ENOMEM if memory allocation for a new node or text storage fails.
 */
int trie_insert(struct trie_node *root, const char *key, const char *value, struct text_expander_data *data) {
    if (!root || !key || !value) { // Null checks.
        return -EINVAL;
    }

    struct trie_node *current = root; // Start from root.

    // Traverse/create path for the key.
    for (int i = 0; key[i] != '\0'; i++) {
        char c = key[i];
        int index = char_to_trie_index(c);
        if (index == -1) { // Invalid character in short code.
            LOG_ERR("Invalid character '%c' (0x%02x) in short code '%s' during insert.", c, c, key);
            return -EINVAL;
        }

        if (!current->children[index]) { // If path doesn't exist, create new node.
            current->children[index] = trie_allocate_node(data);
            if (!current->children[index]) { // Allocation failed.
                LOG_ERR("Failed to allocate trie node for key '%s' at char '%c'.", key, c);
                return -ENOMEM;
            }
        }
        current = current->children[index]; // Move to next node.
    }

    // At this point, `current` is the node corresponding to the end of the `key`.

    // Handle updating an existing expansion.
    if (current->is_terminal && current->expanded_text) {
        size_t new_len = strlen(value) + 1; // +1 for null terminator.
        size_t old_len = strlen(current->expanded_text) + 1;

        // If new text can fit in the space of the old text, reuse the storage.
        if (new_len <= old_len) {
            strcpy(current->expanded_text, value); // Overwrite old text.
            LOG_DBG("Updated existing expansion for '%s' by overwriting in-place.", key);
            return 0; // Successful update.
        }
        // New text is longer. The old text_pool space will be orphaned.
        // The new text will be allocated in a new segment of the text_pool.
        LOG_WRN("New expansion for '%s' ('%s', len %zu) is longer than old ('%s', len %zu). Old text pool space will be orphaned.",
                key, value, new_len, current->expanded_text, old_len);
        // Proceed to allocate new space below.
    }

    // Allocate storage for the expanded text.
    size_t text_len = strlen(value) + 1; // +1 for null terminator.
    current->expanded_text = trie_allocate_text_storage(data, text_len);
    if (!current->expanded_text) { // Text storage allocation failed.
        LOG_ERR("Failed to allocate text storage for value '%s' (key '%s').", value, key);
        // Note: If nodes were created along the path (and were not pre-existing), they are not cleaned up
        // here on this specific failure. This could lead to orphaned nodes if the insert fails at
        // text allocation after new nodes were made for this key. A full `clear_all` would reclaim them.
        // Ensuring sufficient text_pool size is the primary mitigation.
        return -ENOMEM;
    }

    strcpy(current->expanded_text, value); // Copy the value into the allocated space.
    current->is_terminal = true;           // Mark this node as terminal.
    LOG_DBG("Trie: Inserted '%s' -> '%s' at node %p, text at %p",
            key, current->expanded_text, (void*)current, (void*)current->expanded_text);

    return 0; // Success.
}

/**
 * @brief "Deletes" a key (short code) from the trie by marking its node as non-terminal.
 *
 * This implementation does not reclaim the memory used by the `expanded_text` string
 * in the `text_pool`, nor does it free the `trie_node`s themselves from the `node_pool`.
 * The memory becomes "orphaned" until a full `zmk_text_expander_clear_all()` resets the pools.
 * This is a common simplification for embedded systems to avoid complex memory management
 * like reference counting or garbage collection for the trie structure itself.
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string to delete.
 * @return 0 on success (key found and marked as non-terminal).
 * @return -EINVAL if `root` or `key` is NULL, or `key` contains invalid characters.
 * @return -ENOENT if the key is not found in the trie or is not a terminal node.
 */
int trie_delete(struct trie_node *root, const char *key) {
    if (!root || !key) {
        return -EINVAL;
    }

    struct trie_node *current = root;
    // The `path` array is not used in this simplified delete, but was present in original.
    // It would be needed for actual node pruning if that were implemented.
    // struct trie_node *path[MAX_SHORT_LEN]; // MAX_SHORT_LEN from text_expander_internals.h
    // int path_len = 0;

    // Traverse to the node corresponding to the key.
    for (int i = 0; key[i] != '\0' /* && path_len < MAX_SHORT_LEN (if path was used) */; i++) {
        char c = key[i];
        int index = char_to_trie_index(c);
        if (index == -1) { // Invalid character.
            return -EINVAL;
        }

        if (!current->children[index]) { // Path does not exist.
            return -ENOENT; // "No such entry".
        }

        // path[path_len++] = current; // If path tracking was needed for pruning.
        current = current->children[index];
    }

    // After traversal, `current` is the node for the last char of `key`.
    if (!current->is_terminal) {
        // Key exists as a prefix, but not as a stored expansion.
        return -ENOENT;
    }

    current->is_terminal = false;      // Mark as non-terminal.
    current->expanded_text = NULL;     // Clear the pointer to the text (text itself remains in pool).

    LOG_DBG("Marked expansion for '%s' as deleted (node %p made non-terminal).", key, (void*)current);
    
    // Pruning of trie nodes (if they become childless and are not terminal) is not implemented
    // in this version to keep memory management simpler. If it were, one would traverse `path`
    // backwards from `current` to `root`, checking if nodes can be removed.

    return 0; // Success.
}
