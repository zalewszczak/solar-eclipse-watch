#include <pebble.h>
#include "application/app_controller.h"

int main(void) {
  app_controller_init();
  app_event_loop();
  app_controller_deinit();
}
