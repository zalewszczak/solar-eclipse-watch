#include "./big_digital_lookup.h"

// One row per style (see big_digital_lookup.h), one column per digit 0-9
// plus the colon marker at column 10. Written via macros rather than 110
// literal lines -- same "keep the source compact even though the
// compiled table is 100+ entries" approach already used for the
// forecast-offset icons and weather-condition table.
#define TD(style, d) RESOURCE_ID_IMAGE_TALLDIGITS_##style##_##d
#define TD_ROW(style) { TD(style, 0), TD(style, 1), TD(style, 2), TD(style, 3), TD(style, 4), \
                        TD(style, 5), TD(style, 6), TD(style, 7), TD(style, 8), TD(style, 9), \
                        TD(style, 10) }
const uint32_t BIG_DIGITAL_DIGIT_RESOURCES[BIG_DIGITAL_STYLE_COUNT][11] = {
  TD_ROW(0), TD_ROW(1), TD_ROW(2), TD_ROW(3), TD_ROW(4), TD_ROW(5), TD_ROW(6)
};
#undef TD_ROW
#undef TD
