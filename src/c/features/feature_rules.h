#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

// Small generic unit-conversion and string-formatting helpers used by primitive resolution.

int16_t feature_rules_convert_temp(int16_t celsius, uint8_t temp_unit);
int16_t feature_rules_apparent_temp_c(int16_t temp_c, int16_t wind_kmh, uint8_t humidity_pct);
int16_t feature_rules_convert_wind(int16_t kmh, uint8_t wind_speed_unit);
void feature_rules_to_upper_str(char *s);

