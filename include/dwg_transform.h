#ifndef DWG_TRANSFORM_H
#define DWG_TRANSFORM_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standard geometric transformations, dispatched per entity type since
 * each stores its points differently (LINE has start/end, CIRCLE/ARC
 * have a center, TEXT has point+point0, POLYLINE/HATCH/LEADER have a
 * vertex list, SOLID/FACE have 4 points, INSERT has one point). This is
 * ordinary geometry, not something recovered from vecad.dll -- ARC's
 * start_angle/end_angle are rotated along with its center for
 * dwg_entity_rotate, and mirrored (angles swapped and negated) for
 * dwg_entity_mirror, matching standard CAD convention.
 *
 * Angles are in degrees, matching the rest of this engine's convention
 * (see dwg_file_io.h).
 */

void dwg_entity_move(HENTITY hEntity, double dx, double dy, double dz);

void dwg_entity_rotate(HENTITY hEntity,
                       double cx, double cy, double cz,
                       double angle_degrees);

void dwg_entity_scale(HENTITY hEntity,
                      double cx, double cy, double cz,
                      double factor);

/*
 * Independent per-axis scale factors around (cx,cy,cz), matching real
 * vecad.dll's own base scale primitive (vuScalePoint takes separate sx/
 * sy/sz -- see reverse/vu_math_notes.md) rather than the uniform-only
 * dwg_entity_scale above. CIRCLE/ARC radius and TEXT/MTEXT height have
 * no exact meaning under non-uniform scale (this engine has no
 * ellipse-from-circle conversion), so both use the average of sx,sy --
 * exact whenever sx==sy, an approximation otherwise, same policy
 * dwg_entity_explode's INSERT handling already uses for the same
 * reason.
 */
void dwg_entity_scale_xyz(HENTITY hEntity,
                          double cx, double cy, double cz,
                          double sx, double sy, double sz);

/* Mirrors across the line through (x1,y1) and (x2,y2) in the XY plane
   (Z is left unchanged -- 2D mirror, the common CAD case). */
void dwg_entity_mirror(HENTITY hEntity,
                       double x1, double y1,
                       double x2, double y2);

/* Deep-copies hEntity (geometry + layer/color/linetype/ex_data, and for
   POLYLINE/HATCH/LEADER every vertex in the list) into a new entity
   appended to hDwg's main entity list. Returns NULL if hEntity's type
   isn't one dwg_transform knows how to clone (same coverage as the
   other four operations above) or on allocation failure. */
HENTITY dwg_entity_copy(HDWG hDwg, HENTITY hEntity);

/*
 * Explodes hEntity into its constituent primitive entities, appended to
 * hDwg's main entity list. Does NOT remove hEntity itself -- the caller
 * decides whether to keep or discard the original (matches vecad.h's
 * own CadEntityExplode semantics of being a separate step from erase).
 * Returns the number of new entities created (0 if hEntity's type isn't
 * explodable or it was empty/degenerate).
 *
 * POLYLINE: each segment between consecutive vertices becomes a LINE
 * (bulge == 0) or an ARC (bulge != 0, standard DXF/AutoCAD bulge-to-arc
 * conversion: included_angle = 4*atan(bulge), positive bulge = CCW from
 * the segment's first vertex to its second -- see dwg_transform.c's
 * bulge_to_arc for the verified derivation).
 * INSERT: each entity in the referenced block is copied, then scaled
 * by the insert's scale_x/y/z about the origin, rotated by its angle
 * about the origin, and moved to its insertion point, in that order
 * (standard block-reference transform order).
 */
unsigned long dwg_entity_explode(HDWG hDwg, HENTITY hEntity);

/*
 * Trim/extend, scoped to hEntity being a LINE against a LINE/CIRCLE/ARC
 * boundary -- the common case in 2D drafting and the pairing vecad.h's
 * own CadEntityTrim/CadEntityExtend exports imply (trimming/extending
 * one entity against another "cutting"/"boundary" entity). Standard CAD
 * geometry (line-line and line-circle intersection, with an angle-range
 * check against the boundary's own sweep for ARC), not something
 * recovered from vecad.dll.
 *
 * (px, py) is a pick point near hEntity, used the way an interactive
 * CAD trim/extend tool would: for trim, it marks which side/portion of
 * the line is being cut away (the endpoint on that side moves to the
 * nearest valid intersection); for extend, it marks which endpoint to
 * extend (the nearer endpoint is the one moved).
 *
 * dwg_entity_trim requires the intersection to fall strictly inside
 * hEntity's current segment (0 < t < 1). dwg_entity_extend requires it
 * to fall strictly outside (t < 0 or t > 1, on the side nearest the
 * pick point) -- i.e. trim only shortens, extend only lengthens.
 *
 * Both return 1 and modify hEntity's geometry in place if a valid
 * intersection was found and used, or 0 (no change) if hEntity isn't a
 * LINE, hBoundary isn't a supported type, or no qualifying intersection
 * exists.
 */
long dwg_entity_trim(HENTITY hEntity, HENTITY hBoundary, double px, double py);
long dwg_entity_extend(HENTITY hEntity, HENTITY hBoundary, double px, double py);

#ifdef __cplusplus
}
#endif

#endif
