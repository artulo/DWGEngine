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
 * winding order), POLYLINE (straight segments between vertices --
 * bulge/arc segments not tessellated yet, drawn as chords). INSERT
 * itself still draws nothing directly (its own entity carries no
 * visible geometry) -- but the R2000 reader (dwg_r2000_entity_reader.c)
 * now explodes each INSERT's referenced block content (LINE/CIRCLE/
 * ARC/POINT/SOLID) into ordinary transformed entities at READ time, so
 * real "accesorios" (door/window/fixture blocks) show up here as plain
 * entities like anything else, no renderer changes needed for that.
 */
#define DWG_RENDER_BG_COLOR 0x00302821UL /* RGB(33,40,48), COLORREF (0x00BBGGRR) -- Arturo's exact requested value */

/*
 * Camara orbital de proyeccion PARALELA (ortografica, sin division de
 * perspectiva) para navegacion 3D real -- ver reverse notes / sesion
 * 2026-08-26: azimuth/elevation en RADIANES, orbitando alrededor de
 * target. world_to_pixel (dwg_render.c) proyecta (wx,wy,wz) -> (vx,vz)
 * de vista con esta camara ANTES de aplicar el mismo mapeo escala/
 * origen 2D que ya existia -- pasar camera=NULL a dwg_render_to_hdc
 * preserva el comportamiento 2D de siempre sin cambios.
 */
typedef struct _DWG_CAMERA3D
{
    double target_x, target_y, target_z;
    double azimuth;   /* radianes, rotacion alrededor del eje Z mundo */
    double elevation; /* radianes, rotacion alrededor del eje X resultante */
} DWG_CAMERA3D;

void dwg_render_to_hdc(HDWG hDwg, void *hdc, long width, long height,
                       double scale, double origin_x, double origin_y,
                       const DWG_CAMERA3D *camera);

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

/*
 * Igual que dwg_render_get_extents pero tambien devuelve el rango de Z
 * (usado para elegir un target_z de camara razonable al entrar en modo
 * 3D) -- funcion NUEVA en vez de ampliar la firma de la existente para
 * no tocar a ninguno de sus llamadores 2D actuales.
 */
long dwg_render_get_extents_3d(HDWG hDwg, double *min_x, double *min_y, double *min_z,
                               double *max_x, double *max_y, double *max_z);

#ifdef __cplusplus
}
#endif

#endif
