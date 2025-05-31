#ifndef ZMK_BEHAVIOR_TEXT_EXPANDER_H
#define ZMK_BEHAVIOR_TEXT_EXPANDER_H

#include <zephyr/kernel.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text);
int zmk_text_expander_remove_expansion(const char *short_code);
void zmk_text_expander_clear_all(void);
int zmk_text_expander_get_count(void);
bool zmk_text_expander_exists(const char *short_code);

#ifdef __cplusplus
}
#endif

#endif
