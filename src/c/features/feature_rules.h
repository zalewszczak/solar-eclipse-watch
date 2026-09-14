#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

// Small feature-domain rules shared by the value, layout and refresh paths.
// Keeping these here prevents features_layer.c from accumulating unrelated
// conversion/classification helpers while leaving the actual slot machinery
// in one place.

int16_t feature_rules_convert_temp(int16_t celsius, uint8_t temp_unit);
int16_t feature_rules_apparent_temp_c(int16_t temp_c, int16_t wind_kmh, uint8_t humidity_pct);
int16_t feature_rules_convert_wind(int16_t kmh, uint8_t wind_speed_unit);
void feature_rules_to_upper_str(char *s);

bool feature_rules_hourly_vibe_is_scheduled_now(const EclipseData *data, time_t now);
bool feature_rules_content_is_weather_derived(uint8_t content);
bool feature_rules_content_needs_second_refresh(uint8_t content);
