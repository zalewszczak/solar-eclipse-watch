#pragma once

// Coordinates the watchface application lifecycle and cross-module event flow.
// The executable entry point remains in pebble-eclipse-watch.c.
void app_controller_init(void);
void app_controller_deinit(void);
