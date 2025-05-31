#ifndef ZMK_TEXT_EXPANDER_INTERNALS_H
#define ZMK_TEXT_EXPANDER_INTERNALS_H

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS 10
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN 16
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN 256
#endif

#define MAX_EXPANSIONS CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define MAX_SHORT_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define MAX_EXPANDED_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN

#include <zmk/trie.h>

struct text_expander_data {
    struct trie_node *root;
    char current_short[MAX_SHORT_LEN];
    uint8_t current_short_len;
    uint8_t expansion_count;
    struct k_mutex mutex;

    struct trie_node node_pool[MAX_EXPANSIONS * MAX_SHORT_LEN];
    uint16_t node_pool_used;
    char text_pool[MAX_EXPANSIONS * MAX_EXPANDED_LEN];
    uint16_t text_pool_used;
};

extern struct text_expander_data expander_data;

#endif
