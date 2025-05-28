/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * @brief Add or update a text expansion
 * 
 * @param short_code The short code to expand (e.g., "addr")
 * @param expanded_text The text to expand to (e.g., "123 Main Street")
 * @return 0 on success, negative error code on failure
 */
int zmk_text_expander_add_expansion(const char *short_code, const char *expanded_text);

/**
 * @brief Remove a text expansion
 * 
 * @param short_code The short code to remove
 * @return 0 on success, negative error code on failure
 */
int zmk_text_expander_remove_expansion(const char *short_code);

/**
 * @brief Clear all text expansions
 */
void zmk_text_expander_clear_all(void);

/**
 * @brief Get the number of active expansions
 * 
 * @return Number of active expansions
 */
int zmk_text_expander_get_count(void);

/**
 * @brief Check if an expansion exists
 * 
 * @param short_code The short code to check
 * @return true if expansion exists, false otherwise
 */
bool zmk_text_expander_exists(const char *short_code);
