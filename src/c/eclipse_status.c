#include <pebble.h>
#include "eclipse_status.h"
#include "celestial_ephemeris.h"

// True from first contact up to (not including) last contact -- the
// same c1/c4 window eclipse_status_get_text() itself uses to decide
// PHASE_BEFORE_C1 vs PHASE_DONE, exposed here so other call sites
// (shake/startup animation gating, the tick-rate override, Space
// view's celestial-body suppression) can all agree on exactly the
// same definition of "the eclipse is happening right now" instead of
// each re-deriving their own slightly different c1/c4 comparison.
bool eclipse_status_is_active(const EclipseData *d, time_t now) {
  return d->valid && d->has_eclipse && now >= d->c1 && now < d->c4;
}

// ---- status text -------------------------------------------------------

static void fmt_countdown(char *buf, size_t buf_len, const char *label, time_t target, time_t now, bool minutes_only) {
  time_t remaining = target - now;
  if (remaining < 0) remaining = 0;
  int h = (int)(remaining / 3600);
  int m = (int)((remaining % 3600) / 60);
  int s = (int)(remaining % 60);
  if (h > 0) {
    snprintf(buf, buf_len, "%s %dh%02dm", label, h, m);
  } else if (minutes_only) {
    snprintf(buf, buf_len, "%s %dm", label, m);
  } else {
    snprintf(buf, buf_len, "%s %d:%02d", label, m, s);
  }
}

EclipsePhase eclipse_status_get_text(const EclipseData *d, time_t now, char *buf, size_t buf_len, bool live_seconds) {
  if (!d->valid) {
    if (d->error_code == 1) snprintf(buf, buf_len, "No location");
    else if (d->error_code == 2) snprintf(buf, buf_len, "Calc error");
    else if (d->error_code == 3) snprintf(buf, buf_len, "Send failed");
    else snprintf(buf, buf_len, "No data yet");
    return PHASE_NO_ECLIPSE;
  }
  // This field is purely about eclipse phases now -- Moon phase has
  // its own corner content type instead (see corners_layer_update_proc
  // in pebble-eclipse-watch.c). No eclipse today (or today's has
  // already finished) means nothing to report here at all; the caller
  // hides the whole text layer in that case rather than leaving it
  // visible-but-blank.
  if (!d->has_eclipse) {
    buf[0] = '\0';
    return PHASE_NO_ECLIPSE;
  }
  if (d->sunset != 0 && now >= d->sunset && now < d->c4) {
    snprintf(buf, buf_len, "Sun set");
    return PHASE_NIGHT;
  }
  if (now < d->c1) {
    // Before the eclipse actually starts, the screen only ticks every
    // second if something ELSE already needs that (show_seconds, a
    // seconds-precision corner content, ...) -- eclipse_status_is_active()
    // is false out here, so it doesn't itself force a live-seconds
    // subscription (see update_tick_subscription()'s own comment).
    // A seconds-precision "M:SS" readout would just sit frozen for up
    // to a minute at a time in that case, so this falls back to a
    // plain whole-minutes (+hours past 59m) countdown instead, matching
    // however often this label is actually being redrawn.
    fmt_countdown(buf, buf_len, "Starts in", d->c1, now, !live_seconds);
    return PHASE_BEFORE_C1;
  }
  if (now >= d->c4) {
    buf[0] = '\0';
    return PHASE_DONE;
  }

  // From here on an eclipse is actively in progress (between C1 and
  // C4) -- prefix whichever phase label applies with the live "% of
  // Sun covered" so both are visible on the one line. eclipse_status_is_active()
  // is true for this whole stretch, which by itself now forces a live-
  // seconds subscription (see update_tick_subscription()), so these can
  // always afford full M:SS precision.
  char phase_buf[24];
  EclipsePhase phase;
  if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now < d->c2) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality in", d->c2, now, false);
    phase = PHASE_PARTIAL_IN;
  } else if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now >= d->c2 && now < d->c3) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality ends", d->c3, now, false);
    phase = PHASE_TOTAL;
  } else if (now < d->max_t && (d->type == ECLIPSE_TYPE_PARTIAL || d->c2 == 0)) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Peak in", d->max_t, now, false);
    phase = PHASE_PARTIAL_IN;
  } else {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Clears in", d->c4, now, false);
    phase = PHASE_PARTIAL_OUT;
  }

  snprintf(buf, buf_len, "%d%% - %s", celestial_interp_mag_pct(d, now), phase_buf);
  return phase;
}

