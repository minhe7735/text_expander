#ifndef ZMK_EXPANSION_ENGINE_H
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define MAX_EXPANDED_LEN 256
#else
#define MAX_EXPANDED_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#endif

#ifndef CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define TYPING_DELAY 10
#else
#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#endif

struct expansion_work {
    struct k_work_delayable work;
    char expanded_text[MAX_EXPANDED_LEN];
    uint8_t backspace_count;
    bool is_backspace_phase;
    size_t text_index;
};

void expansion_work_handler(struct k_work *work);
int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len);
void cancel_current_expansion(void);
struct expansion_work *get_expansion_work_item(void);

#endif
