#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

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
    LOG_ERR("Trie node pool exhausted. Current usage: %u, Max: %zu. Increase "
            "CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS or CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN.",
            data->node_pool_used, ARRAY_SIZE(data->node_pool));
    return NULL;
  }

  struct trie_node *node = &data->node_pool[data->node_pool_used++];

  memset(node, 0, sizeof(struct trie_node));
  return node;
}

char *trie_allocate_text_storage(struct text_expander_data *data, size_t len) {
  if (data->text_pool_used + len > sizeof(data->text_pool)) {
    LOG_ERR("Text pool exhausted. Requested: %zu, Used: %u, Total: %zu. Increase "
            "CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN or CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS.",
            len, data->text_pool_used, sizeof(data->text_pool));
    return NULL;
  }

  char *text = &data->text_pool[data->text_pool_used];
  LOG_DBG("Allocated %zu bytes from text pool at address %p. Pool used will be: %u", len,
          (void *)text, data->text_pool_used + (uint16_t)len);

  data->text_pool_used += (uint16_t)len;
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

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
  if (!root || !key) {
    return NULL;
  }

  struct trie_node *current = root;

  if (key[0] == '\0') {
    return root;
  }

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

  return current;
}

const char *trie_get_expanded_text(struct trie_node *node) {
  if (node && node->is_terminal) {
    return node->expanded_text;
  }
  return NULL;
}

int trie_insert(struct trie_node *root, const char *key, const char *value,
                struct text_expander_data *data) {
  if (!root || !key || !value) {
    return -EINVAL;
  }

  struct triie_node *current = root;

  for (int i = 0; key[i] != '\0'; i++) {
    char c = key[i];
    int index = char_to_trie_index(c);
    if (index == -1) {
      LOG_ERR("Invalid character '%c' (0x%02x) in short code '%s' during insert.", c, c, key);
      return -EINVAL;
    }

    if (!current->children[index]) {
      current->children[index] = trie_allocate_node(data);
      if (!current->children[index]) {
        LOG_ERR("Failed to allocate trie node for key '%s' at char '%c'.", key, c);
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
      LOG_DBG("Updated existing expansion for '%s' by overwriting in-place.", key);
      return 0;
    }

    LOG_WRN("New expansion for '%s' ('%s', len %zu) is longer than old ('%s', len %zu). Old text "
            "pool space will be orphaned.",
            key, value, new_len, current->expanded_text, old_len);
  }

  size_t text_len = strlen(value) + 1;
  current->expanded_text = trie_allocate_text_storage(data, text_len);
  if (!current->expanded_text) {
    LOG_ERR("Failed to allocate text storage for value '%s' (key '%s').", value, key);

    return -ENOMEM;
  }

  strcpy(current->expanded_text, value);
  current->is_terminal = true;
  LOG_DBG("Trie: Inserted '%s' -> '%s' at node %p, text at %p", key, current->expanded_text,
          (void *)current, (void *)current->expanded_text);

  return 0;
}

int trie_delete(struct trie_node *root, const char *key) {
  if (!root || !key) {
    return -EINVAL;
  }

  struct trie_node *current = root;

  for (int i = 0; key[i] != '\0'; i++) {
    char c = key[i];
    int index = char_to_trie_index(c);
    if (index == -1) {
      return -EINVAL;
    }

    if (!current->children[index]) {
      return -ENOENT;
    }

    current = current->children[index];
  }

  if (!current->is_terminal) {
    return -ENOENT;
  }

  current->is_terminal = false;
  current->expanded_text = NULL;

  LOG_DBG("Marked expansion for '%s' as deleted (node %p made non-terminal).", key, (void *)current);

  return 0;
}
