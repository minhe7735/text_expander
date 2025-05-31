#ifndef ZMK_HID_UTILS_H 
#define ZMK_HID_UTILS_H

#include <stdint.h>  
#include <stdbool.h> 
#include <zmk/hid.h> 
#include <zmk/endpoints.h> 

uint32_t char_to_keycode(char c, bool *needs_shift);

int send_and_flush_key_action(uint32_t keycode, bool pressed);

#endif
