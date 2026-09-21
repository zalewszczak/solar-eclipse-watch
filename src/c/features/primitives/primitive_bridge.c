#include "./primitive_bridge.h"
#include "./primitive_catalogue.h"
#include "../feature_slot.h"
#include <string.h>
#include <stdio.h>

void primitive_bridge_build(PrimitiveFeature *out, const FeatureSlot *slot) {
  memset(out, 0, sizeof(*out));
  if (!slot || slot->segment_count == 0) return;

  int n = slot->segment_count;
  if (n > PRIMITIVE_FEATURE_MAX) n = PRIMITIVE_FEATURE_MAX; // acceptance criterion 2

  for (int i = 0; i < n; i++) {
    const RenderSegment *seg = &slot->segments[i];
    PrimitiveInstance *prim = &out->items[i];

    prim->id = primitive_catalogue_id_for(slot->content, i, slot);
    // Note: the weather-error override in feature_values_compute_slot()
    // (feature_values.c) collapses a weather content's normal segment shape
    // down to a single "ERR n" text segment without changing slot->content,
    // so in that case the catalogue above still returns the content's
    // *normal* (often icon-flavored) primitive ID here while `seg->is_icon`
    // correctly reports false. Harmless today -- primitive_layout.c and
    // primitive_renderer.c only ever look at `kind`/payload/color, never at
    // `id` -- but a future consumer that targets updates by primitive ID
    // (Section 9/29) will need this error path special-cased.
    prim->kind = seg->is_icon ? PRIMITIVE_KIND_ICON : PRIMITIVE_KIND_TEXT;
    // Atomic decomposition (Phase 8): a text segment whose entire content is
    // exactly one of the three universal spacer glyphs (Section 4) is
    // reclassified from generic TEXT into its own kind. Layout/render treat
    // all four kinds identically today (same measurement, same draw path --
    // see primitive_layout.c/primitive_renderer.c's is_text_like()), so this
    // doesn't change anything on screen; it keeps the primitive model
    // honest about *what* each primitive is for future consumers (a
    // Phase 9 settings editor listing "insert a slash" as its own primitive
    // type, say) rather than leaving every separator indistinguishable from
    // arbitrary text.
    if (!seg->is_icon) {
      if (strcmp(seg->text, " ") == 0) { prim->kind = PRIMITIVE_KIND_SPACE; }
      else if (strcmp(seg->text, "/") == 0) { prim->kind = PRIMITIVE_KIND_SLASH; }
      else if (strcmp(seg->text, "-") == 0) { prim->kind = PRIMITIVE_KIND_DASH; }
    }
    prim->source = primitive_catalogue_source_for(prim->id);
    prim->refresh_class = primitive_catalogue_refresh_class_for(prim->id);

    if (seg->is_icon) {
      prim->icon_kind = seg->icon_kind;
    } else {
      snprintf(prim->text, sizeof(prim->text), "%s", seg->text);
    }
    // icon_extra/icon_flag exist on every RenderSegment regardless of kind
    // (see feature_slot.h) -- carried into aux/aux_flag unconditionally so
    // a parametrized text primitive (e.g. the timezone cluster's zone
    // index, atomic content 44-62) can use them too, not just icons.
    prim->aux = seg->icon_extra;
    prim->aux_flag = seg->icon_flag;
    prim->color = seg->color;
    prim->color2 = seg->color2;
    // width/x_offset are left at 0 here -- primitive_layout.c owns them (Phase 5).
  }
  out->count = (uint8_t)n;
}
