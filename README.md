# ZMK Text Expander

## Overview

The ZMK Text Expander is a behavior module for the [ZMK Firmware](https://zmk.dev/) that allows users to define short codes (abbreviations) which automatically expand into longer text phrases. This is useful for frequently typed strings like email addresses, code snippets, or common replies.

The module captures alphanumeric key presses to form a short code. When a designated expander key is pressed (or another trigger condition is met), if the typed short code is recognized, it is deleted via simulated backspaces, and the corresponding expanded text is typed out.

## Features

* **Custom Expansions:** Define your own short codes and the text they expand to.
* **Dynamic Management:** Programmatically add, remove, or clear all expansions at runtime via provided API functions.
* **Trie-based Storage:** Efficiently stores and searches for short codes using a trie data structure.
* **Memory Pooling:** Uses pre-allocated memory pools for trie nodes and expanded text strings to manage memory in an embedded environment.
    * **Memory Reclamation:** Individual expansion removal (`zmk_text_expander_remove_expansion`) or updating an expansion with a longer text string will not immediately reclaim the memory used by the old text or nodes from the pools. This memory becomes "orphaned" but available for reuse after a full reset. The `zmk_text_expander_clear_all()` function is the primary way to reclaim all memory from the pools and reset the expander's state.
* **Asynchronous Expansion:** The process of deleting the short code and typing the expanded text is handled asynchronously by an expansion engine, preventing blocking of the main keyboard processing.
* **Configurable Input Behavior:**
    * **Aggressive Reset Mode:** (Optional) Resets the short code input buffer if the typed sequence doesn't match any known prefix.
    * **Reset Keys:** Configurable behavior for keys like Space, Enter, and Tab to reset the input buffer.
* **Device Tree Configuration:** Predefine a set of expansions directly in your ZMK keymap configuration.

## Components

The text expander module consists of several key components:

* **`text_expander.c` / `include/zmk/text_expander.h` / `include/zmk/text_expander_internals.h`**:
    * The core logic for the text expander behavior.
    * Manages the current short code input buffer.
    * Handles key press events to build and reset the short code.
    * Provides public API functions for managing expansions.
    * Initializes and manages the global `expander_data` structure which holds the trie, memory pools, and current state.
* **`trie.c` / `include/zmk/trie.h`**:
    * Implements a trie (prefix tree) data structure for storing short codes and their associated expanded text.
    * Provides functions for inserting, searching, and deleting entries, as well as allocating nodes and text from memory pools.
* **`expansion_engine.c` / `include/zmk/expansion_engine.h`**:
    * Manages the process of typing out the expanded text.
    * Handles sending backspace events to delete the typed short code.
    * Sequentially sends key presses for each character in the expanded text, with configurable delays.
    * Operates using a Zephyr work queue for asynchronous execution.
* **`hid_utils.c` / `include/zmk/hid_utils.h`**:
    * Utility functions to convert characters to HID keycodes.
    * Handles sending HID key press and release events, including managing the Shift modifier.
* **`dts/bindings/behaviors/zmk,behavior-text-expander.yaml`**:
    * Defines the Device Tree binding for this behavior, allowing users to configure expansions in their `.keymap` files.
* **`zephyr/module.yml`**:
    * Zephyr module manifest, providing metadata for the build system.
* **`CMakeLists.txt`**:
    * CMake script for building the module as part of the ZMK firmware.

## Configuration

### Kconfig Options

Several Kconfig options allow you to customize the text expander module. These are typically set in your ZMK configuration files (e.g., `config/<shield_name>.conf`). Refer to your Kconfig file or the Zephyr Kconfig browser for the exact default values.

* `CONFIG_ZMK_TEXT_EXPANDER` (boolean): Enables or disables the text expander module. This must be set to `y` to use the feature.
* `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS` (int): Maximum number of distinct expansions that can be stored (e.g., default `10`).
* `CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN` (int): Maximum length of a short code (e.g., "eml") (e.g., default `16`).
* `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN` (int): Maximum length of the expanded text (e.g., "my.email@example.com") (e.g., default `256`).
* `CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY` (int): Delay in milliseconds between typed characters during expansion (e.g., default `10`).
* `CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE` (boolean): If enabled, the short code input buffer will reset if the current typed sequence is not a prefix of any defined short code.
* `CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER` (boolean): If enabled (typically the default), pressing Enter will reset the short code input buffer.
* `CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_TAB` (boolean): If enabled (typically the default), pressing Tab will reset the short code input buffer.

### Device Tree Configuration

You can predefine text expansions in your ZMK keymap file (e.g., `<shield_name>.keymap`). The behavior is identified as `zmk,behavior-text-expander`.

Each expansion is a child node with `short_code` and `expanded_text` properties.

**Example:**

```dts
/ {
    behaviors {
        // Define the text expander behavior instance (e.g., "txt_exp")
        txt_exp: text_expander {
            compatible = "zmk,behavior-text-expander";
            // Child nodes define the actual expansions
            my_email: email_expansion {
                short_code = "eml";
                expanded_text = "my.long.email.address@example.com";
            };
            gh_sig: github_signature {
                short_code = "ghs";
                expanded_text = "- Sent from my ZMK keyboard";
            };
            // Add more expansions as needed
        };
    };

    keymap {
        // ... other layers and bindings
        default_layer {
            bindings = <
                // ... other keys
                // Assign the text expander behavior to a key.
                // For example, on a specific key position:
                // &kp A  &kp B  &txt_exp  &kp C  ...
                //
                // Or, if you have a dedicated key (e.g., a macro key or a function layer key)
                // you might assign it directly:
                // &txt_exp // Bound to the desired key
            >;
        };
    };
};
```
## How it Works

1.  **Input Buffering:** As you type alphanumeric characters (lowercase 'a'-'z', numbers '0'-'9'), they are appended to an internal `current_short` buffer.
2.  **Buffer Reset:**
    * Pressing `Spacebar` generally resets the buffer if it's not empty.
    * Other keys (not involved in short code building, like most symbols, function keys, or modifiers if not part of a combo) will also reset the buffer.
    * If `CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE` is enabled, the buffer resets as soon as the typed sequence no longer matches the beginning (prefix) of any stored short code.
    * The behavior of `Enter` and `Tab` regarding buffer reset is configurable via Kconfig.
    * Backspace removes the last character from the buffer.
3.  **Expansion Trigger:** When the key bound to the text expander behavior (e.g., `&txt_exp`) is pressed:
    * The system checks if the `current_short` buffer contains a recognized short code stored in the trie.
    * **If a match is found:**
        * The `expansion_engine` is invoked.
        * The engine first sends the required number of `Backspace` key presses to delete the typed short code from your text input area.
        * Then, it types out each character of the `expanded_text`, respecting the `TYPING_DELAY`.
        * The `current_short` buffer is reset.
    * **If no match is found:** The `current_short` buffer is typically reset.

## Public API

The module provides the following C functions (callable from other ZMK modules or custom code if needed) for managing expansions dynamically:

* `int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text);`
    * Adds or updates an expansion.
* `int zmk_text_expander_remove_expansion(const char *short_code);`
    * Removes an expansion.
* `void zmk_text_expander_clear_all(void);`
    * Clears all stored expansions and resets memory pools.
* `int zmk_text_expander_get_count(void);`
    * Returns the current number of stored expansions.
* `bool zmk_text_expander_exists(const char *short_code);`
    * Checks if an expansion for the given short code exists.

## Building

This text expander is a Zephyr module intended to be compiled as part of ZMK firmware.

* Ensure `CONFIG_ZMK_TEXT_EXPANDER=y` is set in your board's Kconfig file (e.g., `app/boards/shields/my_shield/my_shield.conf` or `config/my_shield.conf`).
* The `CMakeLists.txt` handles the inclusion of source files.
* The `zephyr/module.yml` file declares it as a Zephyr module.

Place this module within your ZMK user configuration's `modules/behaviors` directory (or a similar appropriate location recognized by your ZMK build setup) and ensure your build system is configured to include it.
