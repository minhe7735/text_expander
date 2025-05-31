#ifndef ZMK_TRIE_H // Start of include guard.
#define ZMK_TRIE_H

#include <zephyr/kernel.h> // For Zephyr types if needed by trie implementation.
#include <stdbool.h>       // For bool type.
#include <stdint.h>        // For standard integer types.

// Defines the size of the alphabet for the trie.
// This version supports lowercase letters 'a'-'z' (26) and digits '0'-'9' (10), totaling 36.
#ifndef TRIE_ALPHABET_SIZE
#define TRIE_ALPHABET_SIZE 36
#endif

// Forward declaration of text_expander_data to avoid circular dependencies.
// This structure is defined in text_expander_internals.h and is needed by
// trie allocation functions which use its memory pools.
struct text_expander_data;

/**
 * @brief Structure representing a node in the trie.
 *
 * Each node can have children corresponding to characters in the alphabet,
 * a pointer to the expanded text if this node marks the end of a short code,
 * and a flag indicating if it's a terminal node.
 */
struct trie_node {
    struct trie_node *children[TRIE_ALPHABET_SIZE]; // Array of pointers to child nodes.
    char *expanded_text;                            // Pointer to the null-terminated expanded string if this node is terminal.
                                                    // This points into the text_pool in text_expander_data.
    bool is_terminal;                               // True if this node represents the end of a complete short code.
};

/**
 * @brief Allocates a new trie node from the node_pool in text_expander_data.
 *
 * @param data Pointer to the text_expander_data structure containing the memory pool.
 * @return Pointer to the allocated trie_node, or NULL if the pool is exhausted.
 */
struct trie_node *trie_allocate_node(struct text_expander_data *data);

/**
 * @brief Allocates storage for an expanded text string from the text_pool in text_expander_data.
 *
 * @param data Pointer to the text_expander_data structure containing the memory pool.
 * @param len The length of the text string to allocate (including space for null terminator).
 * @return Pointer to the allocated memory block within the text_pool, or NULL if the pool is exhausted.
 */
char *trie_allocate_text_storage(struct text_expander_data *data, size_t len);

/**
 * @brief Searches the trie for a given key (short code).
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string to search for.
 * @return Pointer to the trie_node if the key is found and is a terminal node, NULL otherwise.
 */
struct trie_node *trie_search(struct trie_node *root, const char *key);

/**
 * @brief Inserts a key-value pair (short code and expanded text) into the trie.
 *
 * If the key already exists, its value might be updated (behavior depends on implementation details,
 * e.g., if new value fits in old space or if reallocation happens).
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string to insert.
 * @param value The null-terminated expanded text string.
 * @param data Pointer to the text_expander_data structure for memory allocation.
 * @return 0 on success.
 * @return -EINVAL if key or value is invalid (e.g., NULL, invalid characters in key).
 * @return -ENOMEM if memory allocation fails.
 */
int trie_insert(struct trie_node *root, const char *key, const char *value, struct text_expander_data *data);

/**
 * @brief Deletes a key (short code) from the trie.
 *
 * This typically involves marking the terminal node as non-terminal.
 * Memory for the node and text itself might not be immediately reclaimed in simple implementations.
 *
 * @param root The root node of the trie.
 * @param key The null-terminated short code string to delete.
 * @return 0 on success.
 * @return -EINVAL if the key is invalid.
 * @return -ENOENT if the key is not found in the trie or is not a terminal node.
 */
int trie_delete(struct trie_node *root, const char *key);

/**
 * @brief Converts a character to its corresponding index in the trie's children array.
 *
 * Maps 'a'-'z' to 0-25 and '0'-'9' to 26-35.
 *
 * @param c The character to convert.
 * @return The trie index (0-35) if the character is valid, -1 otherwise.
 */
int char_to_trie_index(char c);

/**
 * @brief Retrieves the expanded text from a trie node.
 *
 * @param node Pointer to the trie_node.
 * @return Pointer to the null-terminated expanded text if the node is terminal and has text, NULL otherwise.
 */
const char *trie_get_expanded_text(struct trie_node *node);

/**
 * @brief Traverses the trie for the given key (or prefix).
 *
 * This function checks if a given string `key` forms a valid path from the `root`
 * of the trie. It does not require the `key` to be a terminal node (i.e., a complete
 * short code). It's useful for checking if the current input buffer is a prefix
 * of any existing short code.
 *
 * @param root The root node of the trie.
 * @param key The key (which could be a prefix of a full short code) to check.
 * @return The trie_node corresponding to the end of the `key` if the path exists in the trie.
 * Returns `NULL` if `root` or `key` is `NULL`, if the `key` contains invalid characters,
 * or if the `key` does not form a valid path from the `root`.
 * If `key` is an empty string, it returns the `root` node itself, as an empty
 * string is a valid prefix of any key.
 */
struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key);

#endif // ZMK_TRIE_H End of include guard.
