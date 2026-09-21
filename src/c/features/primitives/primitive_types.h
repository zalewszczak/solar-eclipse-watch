#pragma once

// Shared enums for the primitive-based feature architecture.
// See FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md, Sections 1-2 and 14.
//
// A primitive's *kind* says what it draws (text / spacer glyph / icon).
// A primitive's *source* says who determines its value (Section 2): the
// watch itself (watch-runtime), or PKJS (PKJS-value/static). A primitive's
// *refresh class* says how often the watch must recompute/redraw it
// (Section 14). Source and refresh class are independent: a PKJS-value
// primitive is always REFRESH_PKJS_VALUE (it only changes when PKJS pushes
// a new value); a watch-runtime primitive can be STATIC, MINUTE, SECOND,
// EVENT or FAST_DYNAMIC depending on how often the watch-local value
// changes.

#include <pebble.h>

typedef enum {
  PRIMITIVE_KIND_TEXT = 0,  // variable-length static text payload (Section 4)
  PRIMITIVE_KIND_SPACE,     // universal single-space glyph (Section 4)
  PRIMITIVE_KIND_SLASH,     // universal "/" glyph (Section 4)
  PRIMITIVE_KIND_DASH,      // universal "-" glyph (Section 4)
  PRIMITIVE_KIND_ICON,      // static icon, payload is an icon ID (Section 4)
} PrimitiveKind;

typedef enum {
  PRIMITIVE_SOURCE_WATCH_RUNTIME = 0, // watch must calculate/obtain the value itself (Section 2.1)
  PRIMITIVE_SOURCE_PKJS_VALUE,        // PKJS supplies the final display-ready value (Section 2.2)
} PrimitiveSource;

typedef enum {
  PRIMITIVE_REFRESH_STATIC = 0, // does not change without an explicit external push
  PRIMITIVE_REFRESH_MINUTE,     // watch-runtime, refresh on the minute tick
  PRIMITIVE_REFRESH_SECOND,     // watch-runtime, refresh every second
  PRIMITIVE_REFRESH_EVENT,      // watch-runtime, refresh on a specific service event (BT/health/battery/quiet time)
  PRIMITIVE_REFRESH_FAST_DYNAMIC, // watch-runtime, refresh on its own fast cadence (compass)
  PRIMITIVE_REFRESH_PKJS_VALUE, // PKJS-value/static: watch just stores whatever PKJS last sent (Section 9)
} PrimitiveRefreshClass;
