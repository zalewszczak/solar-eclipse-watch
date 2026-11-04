#include "./primitive_transport.h"
#include "./primitive_catalogue.h"
#include "./primitive_resolver.h"
#include "../feature_color_helpers.h"
#include <string.h>
#include <stdio.h>

bool primitive_transport_try_build(PrimitiveFeature *out, const uint8_t *primitive_ids, const uint8_t *primitive_aux,
                                   const EclipseData *data, time_t now, struct tm *t, uint8_t color_mode,
                                   GColor main_color, GColor accent_color) {
  memset(out, 0, sizeof(*out));
  if (primitive_ids[0] == PRIM_ID_NONE) return false; // not sent for this slot

  int n = 0;
  for (int i = 0; i < PRIMITIVE_FEATURE_MAX; i++) {
    PrimitiveId id = (PrimitiveId)primitive_ids[i];
    if (id == PRIM_ID_NONE) break;

    PrimitiveResolved r;
    if (!primitive_resolver_resolve(id, primitive_aux[i], data, now, t, &r)) {
      // One unresolvable primitive means the whole sequence can't be
      // trusted yet (see primitive_transport.h) -- bail rather than show
      // 4 of 5 primitives and silently drop the 5th.
      return false;
    }

    PrimitiveInstance *p = &out->items[n];
    p->id = id;
    p->kind = r.kind;
    p->refresh_class = primitive_catalogue_refresh_class_for(id);
    p->aux = r.aux;
    p->aux_flag = r.aux_flag;
    if (r.kind == PRIMITIVE_KIND_ICON) {
      p->icon_kind = r.icon_kind;
    } else {
      snprintf(p->text, sizeof(p->text), "%s", r.text);
    }
    // feature_color_resolve_flat already implements the MONO/ACC/
    // PILL/COLOR dispatch every legacy compute function uses -- COLOR mode
    // gets this primitive's own resolved color, every other mode ignores
    // it in favor of main/accent. Same function, same behavior, just
    // called once per primitive here instead of once per whole feature.
    // force_color (see primitive_resolver.h) skips that dispatch entirely
    // for the rare primitive whose legacy code did too (an "N/A" fallback
    // that's always gray, not just in COLOR mode).
    GColor dyn = r.dynamic_is_main ? main_color : r.dynamic_color;
    p->color = r.force_color ? r.dynamic_color
             : feature_color_resolve_flat(color_mode, dyn, main_color, accent_color);
    // The old segment helpers set color2 = color; this
    // path memset()s to 0 (transparent), which would break any icon that
    // reads color2. Same default here; only the compass overrides it.
    p->color2 = p->color;
    if (r.two_tone_accent_main && color_mode == 3) {
      p->color = accent_color;  // north arrow
      p->color2 = main_color;   // the other three arrows
    }
    n++;
  }
  out->count = (uint8_t)n;
  return true;
}
