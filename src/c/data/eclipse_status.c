#include <pebble.h>
#include "./eclipse_status.h"
#include "../rendering/background/celestial_ephemeris.h"

// True during the [C1, C4) eclipse interval.
bool eclipse_status_is_active(const EclipseData *d, time_t now) {
  return d->valid && d->has_eclipse && now >= d->c1 && now < d->c4;
}

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
  if (!d->has_eclipse) {
    buf[0] = '\0';
    return PHASE_NO_ECLIPSE;
  }
  if (d->sunset != 0 && now >= d->sunset && now < d->c4) {
    snprintf(buf, buf_len, "Sun set");
    return PHASE_NIGHT;
  }
  if (now < d->c1) {
    fmt_countdown(buf, buf_len, "Starts in", d->c1, now, !live_seconds);
    return PHASE_BEFORE_C1;
  }
  if (now >= d->c4) {
    buf[0] = '\0';
    return PHASE_DONE;
  }

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
