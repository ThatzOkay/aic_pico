/*
 * AIME Protocol
 * WHowe <github.com/whowechina>
 */

#ifndef AIME_H
#define AIME_H

#include <stdint.h>
#include <stdbool.h>

/* return true if accepts a byte, false if rejects */
typedef void (*aime_putc_func)(uint8_t byte);

void aime_init(aime_putc_func putc_func);
void aime_virtual_aic(bool enable);
void aime_set_mode(int mode);
const char *aime_get_mode_string();

bool aime_feed(int c);

/* aime activity expires at this time */
uint64_t aime_expire_time();

uint32_t aime_led_color();

#endif