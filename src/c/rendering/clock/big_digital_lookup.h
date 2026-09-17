#pragma once
#include <pebble.h>

// Big Digital reuses CLOCK_FONT for style selection rather than a new
// field: a clock_font value of 100 or above means "Big Digital style
// (value - 100)" instead of a real text font -- see big_digital_display.c
// for where that subtraction happens. This table then maps that 0-9
// style index to its 11 digit-bitmap resource ids: 0-9 are the actual
// digits, and 10 is the colon marker (drawn at the same size as every
// other digit -- see big_digital_display.c's own comment on why -- with
// its visible art just a thin mark in the middle of an otherwise
// transparent canvas).
#define BIG_DIGITAL_STYLE_COUNT 5
#define BIG_DIGITAL_COLON_INDEX 10

extern const uint32_t BIG_DIGITAL_DIGIT_RESOURCES[BIG_DIGITAL_STYLE_COUNT][11];
