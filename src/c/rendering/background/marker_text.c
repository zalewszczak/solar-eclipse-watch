#include "./marker_text.h"
#include "./marker_layer.h"
#include <string.h>

static void int_to_roman(int num, char *buf, size_t buf_size) {
  if (num <= 0) { snprintf(buf, buf_size, "%d", num); return; }
  static const int VALUES[] = {50, 40, 10, 9, 5, 4, 1};
  static const char *SYMBOLS[] = {"L", "XL", "X", "IX", "V", "IV", "I"};
  size_t pos = 0;
  buf[0] = '\0';
  for (int i = 0; i < 7 && num > 0; i++) {
    while (num >= VALUES[i]) {
      size_t len = strlen(SYMBOLS[i]);
      if (pos + len + 1 > buf_size) return; // out of room -- truncate rather than overflow
      memcpy(buf + pos, SYMBOLS[i], len);
      pos += len;
      buf[pos] = '\0';
      num -= VALUES[i];
    }
  }
}


void marker_text_draw(GContext *ctx, GPoint center, GRect screen, FontSlot *font_slot,
                               const MarkerTextConfig *text_cfg, const MarkerRingConfig *hour_cfg,
                               const MarkerRingConfig *second_cfg, GColor color,
                               bool anim_active, int32_t anim_overall_progress_1000,
                               bool draw_debug) {
  if (text_cfg->target == 0) return;
  bool is_hour = (text_cfg->target == 1);
  const MarkerRingConfig *ring = is_hour ? hour_cfg : second_cfg;
  uint16_t mask = is_hour ? text_cfg->hour_mask : text_cfg->second_mask;
  if (mask == 0) return;
// Same "hour only" scoping as marker_layer_draw()'s own two ring
  if (!is_hour) { anim_active = false; anim_overall_progress_1000 = 0; }

  GFont font = font_lookup_resolve(font_slot, text_cfg->font_choice);
  int16_t fh = font_lookup_height(text_cfg->font_choice) + font_lookup_y_offset(text_cfg->font_choice);
// Size the box from the selected font rather than a fixed pixel height.
  int16_t box_w = fh * 2 + 8, box_h = fh + 6;

  graphics_context_set_text_color(ctx, color);

  for (int i = 0; i < 12; i++) {
    if (!(mask & (1 << i))) continue;
    
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / 12) & 0xFFFF;
    int offset_text_pct = ring->thickness == 0 ? 100 + text_cfg->offset_px : ring->inner_border_pct + text_cfg->offset_px;
    if(offset_text_pct>100) {
      offset_text_pct = 100;
    } else if (offset_text_pct < 0) {
      offset_text_pct = 0;
    }
    GPoint pos = marker_layer_point_on_ring(center, screen, angle, offset_text_pct, ring->thickness == 0 ? 100 : ring->inner_eccentricity);
   
// int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

    char buf[8];
    int label = is_hour ? (i == 0 ? 12 : i) : (i * 5);
// "Animate background on start": each label counts up from 0 to
    if (anim_active) {
      int32_t local = marker_anim_mark_progress_1000_raw(i, 12, anim_overall_progress_1000);
      int32_t eased = marker_layer_ease_in_1000(local);
      label = (int)(((int32_t)label * eased) / 1000);
// Hour markers never legitimately target 0 (i==0 maps to 12
      if (label == 0) continue;
    }
    if (text_cfg->roman_numerals && label > 0) int_to_roman(label, buf, sizeof(buf));
    else if (text_cfg->roman_numerals) buf[0] = '\0'; // roman numerals have no glyph for 0 -- blank rather than garbage mid-count-up
    else snprintf(buf, sizeof(buf), "%d", label);

    GRect box = GRect(pos.x - box_w / 2, pos.y - box_h / 2, box_w, box_h);
    graphics_draw_text(ctx, buf, font, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    
    if (draw_debug){
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_stroke_color(ctx, GColorYellow);
      
      graphics_draw_rect(ctx, box);
      graphics_context_set_stroke_color(ctx, GColorOrange);
      graphics_draw_rect(ctx, GRect(pos.x-15, pos.y, 30, 1));
      graphics_draw_rect(ctx, GRect(pos.x, pos.y-15, 1, 30));
    }
  }
}

