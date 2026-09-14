#include "background_overlays.h"
#include "eclipse_ui.h"
#include "marker_layer.h"

void background_overlays_draw_marker_animation(GContext *ctx, MarkerLayerState *markers, const EclipseData *d,
                                                GRect bounds, time_t now, uint16_t elapsed_ms, uint16_t duration_ms) {
  if (d->bottom_style != 1) return;
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
  int32_t progress_1000 = ((int32_t)elapsed_ms * 1000) / duration_ms;
  if (progress_1000 > 1000) progress_1000 = 1000;
  marker_layer_draw(ctx, markers, center, bounds, d, main_color, accent_color, bg, true, progress_1000, d->draw_debug);
}

