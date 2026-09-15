#pragma once

// Coordinates the watchface application lifecycle and cross-module event flow.
// The executable entry point remains in application entry point; this
// controller owns the application lifecycle after main() starts.
void app_controller_init(void);
void app_controller_deinit(void);
