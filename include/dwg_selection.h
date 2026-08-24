#ifndef DWG_SELECTION_H
#define DWG_SELECTION_H

#include "dwg_types.h"
#include "dwg_document.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pick/window/batch-edit operations built on top of the low-level
 * selection-set storage in dwg_document.h (dwg_document_sel_add/clear/
 * count/get). Mirrors real vecad.dll's global CadSelectByDist/
 * CadSelectByPolygon/CadSelectByLayer (add to the selection) and
 * CadSelMove/Rotate/Scale/Mirror/Erase/Explode/Copy (act on it) -- see
 * reverse/vecad.h's CadSel* exports -- but scoped per-document instead
 * of process-global, and returning counts instead of void.
 *
 * Hit-testing is standard 2D CAD picking geometry (point-to-segment
 * distance, point-to-circle distance with an arc-sweep check, an
 * axis-aligned bounding box for window/crossing select), not anything
 * recovered from vecad.dll. Known simplifications, documented rather
 * than silently assumed:
 *  - TEXT/MTEXT/INSERT hit-test against their insertion point only (no
 *    real font metrics or per-block bounding box).
 *  - SOLID/FACE/HATCH/LEADER hit-test against their edges (like a
 *    polyline boundary), not true fill/inside testing.
 *  - POLYLINE bulge segments are hit-tested against their straight
 *    chord, not the true arc (picking tolerance only, not used by
 *    dwg_entity_explode which does use the real arc).
 *  - Window select (crossing == 0) is exact: an entity is selected only
 *    if its bounding box is fully inside the rectangle, which guarantees
 *    the real geometry is too. Crossing select (crossing != 0) is an
 *    approximation: an entity is selected if its bounding box merely
 *    overlaps the rectangle, which can over-select (e.g. a diagonal
 *    LINE whose bbox overlaps the rectangle while the line itself
 *    misses it) -- standard practice for a first-pass crossing test,
 *    not exact polygon clipping.
 */

/* Adds every entity within tolerance of (px,py) to hDwg's selection
   (does not clear any existing selection first). Returns how many were
   newly added (0 if none hit, already-selected entities don't count
   twice). Mirrors CadSelectByDist. */
unsigned long dwg_select_point(HDWG hDwg, double px, double py, double tolerance);

/* Adds every entity whose bounding box relationship to the rectangle
   (x1,y1)-(x2,y2) matches (see the crossing-mode caveat above) to
   hDwg's selection. Returns how many were newly added. Mirrors
   CadSelectByPolygon (rectangular case). */
unsigned long dwg_select_window(HDWG hDwg, double x1, double y1, double x2, double y2, DWG_BOOL crossing);

/* Adds every entity on the named layer to hDwg's selection. Returns how
   many were newly added. Mirrors CadSelectByLayer. */
unsigned long dwg_select_layer(HDWG hDwg, const char *layer_name);

/* Applies the corresponding dwg_entity_* transform (see dwg_transform.h)
   to every currently selected entity. Mirror CadSelMove/Rotate/Scale/
   Mirror. */
void dwg_sel_move(HDWG hDwg, double dx, double dy, double dz);
void dwg_sel_rotate(HDWG hDwg, double cx, double cy, double cz, double angle_degrees);
void dwg_sel_scale(HDWG hDwg, double cx, double cy, double cz, double factor);
void dwg_sel_mirror(HDWG hDwg, double x1, double y1, double x2, double y2);

/* Removes and destroys every currently selected entity, then clears the
   selection. Returns how many were erased. Mirrors CadSelErase. */
unsigned long dwg_sel_erase(HDWG hDwg);

/* Explodes every currently selected entity (see dwg_entity_explode --
   only POLYLINE/INSERT actually produce anything). The exploded
   originals are left in place, matching dwg_entity_explode's own
   contract; the selection itself is unchanged. Returns the total number
   of new entities created. Mirrors CadSelExplode. */
unsigned long dwg_sel_explode(HDWG hDwg);

/* Deep-copies every currently selected entity (see dwg_entity_copy) and
   replaces the selection with the new copies -- matching the common CAD
   "copy selection" workflow where the copies become active for further
   editing. Returns how many copies were made (entities dwg_entity_copy
   can't clone are skipped, same coverage as dwg_entity_copy itself).
   Mirrors CadSelCopy. */
unsigned long dwg_sel_copy(HDWG hDwg);

/*
 * Joins connected chains of currently-selected LINE/ARC entities (two
 * entities are connected when an endpoint of one lands within
 * tolerance of an endpoint of the other, comparing X/Y only -- this
 * engine's 2D/elevation model) into new POLYLINE entities, one per
 * chain of 2 or more. Mirrors CadSelJoin (see reverse/vecad.h -- the
 * real export takes the same tolerance parameter; its internal chain-
 * building algorithm wasn't traced, this is standard graph-chasing,
 * not reconstructed from vecad.dll). ARC segments become bulged
 * POLYLINE segments via the inverse of the bulge-to-arc conversion in
 * dwg_entity_explode (same verified convention: positive bulge = CCW).
 *
 * Scope, documented rather than silently assumed: only LINE and ARC
 * participate (matching dwg_entity_trim/extend's own LINE/ARC-only
 * scope) -- POLYLINE, and every other entity type, are left in the
 * selection untouched, not merged. A LINE/ARC with no matching
 * neighbor also stays untouched. Chain-building doesn't handle
 * branching (three segments meeting at one point picks one pairing
 * arbitrarily, standard behavior for a first-pass JOIN).
 *
 * On return, the selection is replaced with: every newly created
 * POLYLINE, plus every originally-selected entity that wasn't merged
 * into one (unmatched LINE/ARC, or any other type). Returns the number
 * of new POLYLINE entities created (0 if nothing joined).
 */
unsigned long dwg_sel_join(HDWG hDwg, double tolerance);

#ifdef __cplusplus
}
#endif

#endif
