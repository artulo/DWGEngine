#ifndef DWG_RENDER_H
#define DWG_RENDER_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GDI rendering for a plain top/plan view (world X -> screen X, world Y
 * -> screen Y inverted -- the "mode 0" projection confirmed from real
 * vecad.dll in reverse/vecad_viewport_notes.md; the other 5 fixed
 * views + axonometric mode documented there aren't implemented here
 * yet, this is a first working viewer, not a full CAD view system).
 *
 * `scale` is world units per pixel (matches vecad's own convention,
 * confirmed via CadDistWinToModel: model_distance = win_pixels * scale
 * -- smaller scale = more zoomed in). `origin_x`/`origin_y` is the
 * world point that maps to the CLIENT AREA's top-left pixel (0,0):
 *
 *   pixel_x = (world_x - origin_x) / scale
 *   pixel_y = (origin_y - world_y) / scale   -- Y-flip: screen Y grows
 *                                                down, world Y grows up
 *
 * Draws directly into an existing HDC (caller owns creating/selecting
 * a bitmap into it for double-buffering, same architecture vecad.dll
 * itself used -- see vecad_viewport_notes.md's "Paint routine" entry).
 * Fills the client rect with DWG_RENDER_BG_COLOR first -- a dark
 * slate (RGB 33,40,48), Arturo's own explicit choice: dark enough to
 * give every real ACI index color its full intended contrast (like a
 * real CAD viewer's dark canvas convention), without being pure black.
 *
 * Only entities this engine's document model supports are drawn:
 * LINE, CIRCLE, ARC (tessellated into segments, not GDI's Arc(), to
 * avoid its logical-coordinate-direction ambiguity under a Y-flipped
 * mapping), POINT (small cross), TEXT/MTEXT (TextOutA, rotation via
 * CreateFont's escapement), SOLID (filled quad, DXF's p1-p2-p4-p3
 * winding order), POLYLINE (straight segments and bulge/arc segments
 * tessellated into line segments), ELLIPSE (parametric tessellation),
 * 3DFACE (filled quad), LEADER (vertex chain as connected line
 * segments). INSERT
 * itself still draws nothing directly (its own entity carries no
 * visible geometry) -- but the R2000 reader (dwg_r2000_entity_reader.c)
 * now explodes each INSERT's referenced block content (LINE/CIRCLE/
 * ARC/POINT/SOLID) into ordinary transformed entities at READ time, so
 * real "accesorios" (door/window/fixture blocks) show up here as plain
 * entities like anything else, no renderer changes needed for that.
 */
#define DWG_RENDER_BG_COLOR 0x00302821UL /* RGB(33,40,48), COLORREF (0x00BBGGRR) -- Arturo's exact requested value */

void dwg_render_to_hdc(HDWG hDwg, void *hdc, long width, long height,
                       double scale, double origin_x, double origin_y);

/*
 * World-space bounding box of every entity in hDwg (min/max over all
 * entities this renderer knows how to measure -- same type coverage as
 * dwg_render_to_hdc). Returns 0 (and leaves the outputs untouched) if
 * hDwg has no measurable entities. Used to implement "zoom to fit"
 * (compute scale/origin from this rather than requiring the caller to
 * already know the drawing's extents).
 */
long dwg_render_get_extents(HDWG hDwg, double *min_x, double *min_y,
                            double *max_x, double *max_y);

#ifdef __cplusplus
}
#endif

#endif
