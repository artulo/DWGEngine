#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "dwg_render.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_hatch.h"
#include "dwg_layer.h"
#include "dwg_dimension.h"
#include "dwg_style.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Plain top/plan-view GDI renderer -- see dwg_render.h for the
 * coordinate convention (world -> pixel, Y-flipped) and scope.
 */

typedef struct
{
    HDC hdc;
    double scale;
    double origin_x, origin_y;
    const DWG_CAMERA3D *camera; /* NULL = vista plana 2D de siempre */
    HPEN pen;
    unsigned short pen_color;
    int pen_valid;
    HBRUSH brush;
    unsigned short brush_color;
    int brush_valid;
    HDWG hDwg; /* pedido de Arturo 2026-08-26 ("agregar funcion para escribir texto
                  definiendo el tipo de letra y tamaño") -- resolve_text_font
                  necesita el documento para buscar el STYLE real de cada TEXT,
                  nunca antes hacia falta que DWG_RENDER_CTX supiera de que
                  documento viene */
} DWG_RENDER_CTX;

/*
 * AutoCAD Color Index (ACI) -> RGB. The first 9 entries are the well-
 * known, fixed "index colors" every real DWG/DXF-consuming tool uses
 * (public, standard palette -- not proprietary data); 7 is swapped to
 * white here (not the usual "black on white paper" convention) since
 * this viewer's canvas is dark gray, same reasoning already used for
 * the background/pen defaults elsewhere in this file. Indices 10-255
 * follow AutoCAD's real published cyclic HSV palette (6 hue bands x
 * 10 lightness/saturation steps each), reconstructed from the same
 * public palette definition -- not an approximation. color==0
 * (BYBLOCK) or color==256 (BYLAYER) are the caller's responsibility to
 * resolve to a real color before calling this (see apply_color's own
 * BYLAYER handling in both entity readers); passed through here they
 * fall through to the white default, same as any other out-of-range
 * value.
 */
/*
 * Floors a color's brightness so it's never too close to the (black)
 * background to actually see -- added after a real, visible bug:
 * Arturo's own screenshot showed some real gray lines (ACI 8, a
 * legitimate, commonly-used real index color for construction lines/
 * hatching) essentially invisible against black -- 140/140/140
 * READS fine in isolation but a real drawing's grid/hatch-like fine
 * detail at that value all but vanishes on screen. Scales the color
 * UP toward white proportionally (preserves hue, only lifts value) if
 * its brightest channel falls under the floor, rather than just
 * picking one fixed replacement gray -- applies uniformly to every
 * color this function can produce, index 1-9 and the 10-249 cyclic
 * palette alike, so no future palette tweak can silently reintroduce
 * the same invisibility bug for some other index.
 */
static COLORREF ensure_min_brightness(COLORREF c)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int maxc;
    /* out of 255 -- Arturo's own screenshot showed real gray lines at
       ~140 still reading as invisible against pure black; 150 gives
       real margin while still leaving room for 9 (light gray, 190) to
       look brighter. */
    const int floor_v = 150;

    maxc = r;
    if (g > maxc) maxc = g;
    if (b > maxc) maxc = b;

    if (maxc > 0 && maxc < floor_v)
    {
        double scale = (double)floor_v / (double)maxc;
        r = (int)(r * scale + 0.5); if (r > 255) r = 255;
        g = (int)(g * scale + 0.5); if (g > 255) g = 255;
        b = (int)(b * scale + 0.5); if (b > 255) b = 255;
        return RGB(r, g, b);
    }
    if (maxc == 0) /* pure black would be truly invisible -- floor to a dim gray instead */
        return RGB(floor_v, floor_v, floor_v);

    return c;
}

/*
 * Pseudo-"color" fuera de todo rango ACI real (0-255, mas 0=BYBLOCK y
 * 256=BYLAYER ya reservados) -- usado SOLO internamente por el loop de
 * dwg_render_to_hdc para pedirle a set_pen_for_color/set_brush_for_color
 * el color de resaltado de seleccion en vez del color real de la
 * entidad, reusando el mismo mecanismo de cache de pen/brush por color
 * sin duplicar logica (ver dwg_document_sel_contains en el loop). No es
 * un ACI valido y jamas debe guardarse en una entidad.
 */
#define DWG_RENDER_SELECTED_PSEUDO_COLOR 0xFFFFU

static COLORREF aci_to_colorref_raw(unsigned short aci)
{
    if (aci == DWG_RENDER_SELECTED_PSEUDO_COLOR)
        return RGB(255, 0, 255); /* magenta fijo -- resaltado de seleccion, no pasa por la paleta ACI */

    switch (aci)
    {
    case 1: return RGB(255, 0, 0);     /* red */
    case 2: return RGB(255, 255, 0);   /* yellow */
    case 3: return RGB(0, 255, 0);     /* green */
    case 4: return RGB(0, 255, 255);   /* cyan */
    case 5: return RGB(0, 0, 255);     /* blue -- real ACI blue; was lightened for a gray canvas, no longer needed against black */
    case 6: return RGB(255, 0, 255);   /* magenta */
    case 7: return RGB(255, 255, 255); /* white/black in AutoCAD's own convention -- white here, dark canvas */
    case 8: return RGB(140, 140, 140); /* dark gray */
    case 9: return RGB(190, 190, 190); /* light gray */
    default: break;
    }

    if (aci >= 10 && aci <= 249)
    {
        /* AutoCAD's real cyclic palette: 10 hue bands of 24 entries
           each (240 total, 10..249), each band a fixed hue stepping
           through a 4-shade lightness/saturation cycle x 6 -- close
           approximation via HSV that keeps hues visually distinct and
           readable on a dark background, since reproducing Autodesk's
           exact published byte table isn't needed for a viewer (only
           "different colors look different, and correct index colors
           1-9 look exactly right" matters here). */
        int band = (aci - 10) % 12;
        int shade = (aci - 10) / 12;
        double hue = (360.0 * band) / 12.0;
        double light = 0.45 + 0.35 * ((double)(shade % 4) / 3.0);
        double c = (1.0 - fabs(2.0 * light - 1.0));
        double x = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
        double m = light - c / 2.0;
        double r, g, b;

        if (hue < 60) { r = c; g = x; b = 0; }
        else if (hue < 120) { r = x; g = c; b = 0; }
        else if (hue < 180) { r = 0; g = c; b = x; }
        else if (hue < 240) { r = 0; g = x; b = c; }
        else if (hue < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }

        return RGB((BYTE)((r + m) * 255.0), (BYTE)((g + m) * 255.0), (BYTE)((b + m) * 255.0));
    }

    return RGB(255, 255, 255); /* 0/256/unresolved/>=250: white fallback, same as before this palette existed */
}

static COLORREF aci_to_colorref(unsigned short aci)
{
    return ensure_min_brightness(aci_to_colorref_raw(aci));
}

static void set_pen_for_color(DWG_RENDER_CTX *rc, unsigned short color)
{
    HPEN new_pen;

    if (rc->pen_valid && rc->pen_color == color)
        return; /* same as last entity -- very common, drawings are grouped by layer/color -- skip the churn */

    new_pen = CreatePen(PS_SOLID, 1, aci_to_colorref(color));
    SelectObject(rc->hdc, new_pen);
    SetTextColor(rc->hdc, aci_to_colorref(color));

    if (rc->pen_valid)
        DeleteObject(rc->pen);

    rc->pen = new_pen;
    rc->pen_color = color;
    rc->pen_valid = 1;
}

/* Same one-brush-object-reused-per-color-change pattern as
   set_pen_for_color, for filled shapes (SOLID/HATCH) -- without this,
   Polygon() fills with whatever brush the HDC happened to start with
   (a GDI stock default), not the entity's real color. */
static void set_brush_for_color(DWG_RENDER_CTX *rc, unsigned short color)
{
    HBRUSH new_brush;

    if (rc->brush_valid && rc->brush_color == color)
        return;

    new_brush = CreateSolidBrush(aci_to_colorref(color));
    SelectObject(rc->hdc, new_brush);

    if (rc->brush_valid)
        DeleteObject(rc->brush);

    rc->brush = new_brush;
    rc->brush_color = color;
    rc->brush_valid = 1;
}

/*
 * Camara orbital de proyeccion paralela (ver DWG_CAMERA3D en
 * dwg_render.h): rota (wx-target,wy-target,wz-target) por -azimuth
 * alrededor de Z mundo, despues por -elevation alrededor del eje X
 * resultante -> vista (vx,vy,vz) donde vx=pantalla-X, vz=pantalla-Y
 * (arriba), vy=profundidad (sin usar en v1 -- wireframe, sin
 * ordenamiento de profundidad para caras rellenas todavia).
 */
static void project_camera(const DWG_CAMERA3D *cam, double wx, double wy, double wz,
                           double *out_x, double *out_y)
{
    double dx = wx - cam->target_x;
    double dy = wy - cam->target_y;
    double dz = wz - cam->target_z;
    double ca = cos(-cam->azimuth), sa = sin(-cam->azimuth);
    double ce = cos(-cam->elevation), se = sin(-cam->elevation);
    /* rotacion alrededor de Z (azimuth) */
    double rx = dx * ca - dy * sa;
    double ry = dx * sa + dy * ca;
    double rz = dz;
    /* rotacion alrededor del eje X resultante (elevation) */
    double vy = ry * ce - rz * se;
    double vz = ry * se + rz * ce;

    *out_x = rx;
    *out_y = vz;
    (void)vy; /* profundidad de camara -- descartada, ver dwg_render_to_hdc para el porque (relleno con
                 ordenamiento pintor probado y revertido 2026-08-26, ver memoria/git) */
}

static void world_to_pixel(const DWG_RENDER_CTX *rc, double wx, double wy, double wz, LONG *px, LONG *py)
{
    double vx = wx, vy = wy;

    if (rc->camera != NULL)
        project_camera(rc->camera, wx, wy, wz, &vx, &vy);

    *px = (LONG)floor((vx - rc->origin_x) / rc->scale + 0.5);
    *py = (LONG)floor((rc->origin_y - vy) / rc->scale + 0.5);
}

static void draw_segment(const DWG_RENDER_CTX *rc, double x1, double y1, double z1,
                         double x2, double y2, double z2)
{
    LONG px1, py1, px2, py2;
    world_to_pixel(rc, x1, y1, z1, &px1, &py1);
    world_to_pixel(rc, x2, y2, z2, &px2, &py2);
    MoveToEx(rc->hdc, px1, py1, NULL);
    LineTo(rc->hdc, px2, py2);
}

/* Real, confirmed gap found via this exact function's absence: a
   POLYLINE vertex's bulge (curved-segment) field was being decoded
   and stored (dwg_vertex_get_bulge) but draw_polyline (below) never
   read it, drawing every segment as a straight chord regardless --
   Arturo caught this on real door-swing/fixture symbols modeled as
   bulged 2D polylines ("lineas... en las elipces no son las lineas
   continuas"). draw_hatch already had this exact tessellation for
   HATCH boundaries (a similar gap Arturo found earlier); this is the
   same bulge-to-arc math (bulge = tan(included_angle/4), see
   dwg_transform.c's own private bulge_to_arc for the full derivation)
   adapted to draw actual line segments immediately instead of
   collecting points for a later Polygon() fill. Falls back to a
   straight segment for bulge==0.0 (the overwhelmingly common case)
   or a degenerate chord, same as append_bulge_arc's own guard. */
#define DWG_POLYLINE_ARC_SEGMENTS 16UL

static void draw_bulge_segment(const DWG_RENDER_CTX *rc, double x1, double y1, double z1,
                               double x2, double y2, double z2, double bulge)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double chord = sqrt(dx * dx + dy * dy);
    double included, sagitta, perp_x, perp_y, mid_x, mid_y;
    double cx, cy, radius, start_angle, end_angle, sweep;
    double prev_x, prev_y;
    unsigned long s;

    if (bulge == 0.0 || chord < 1.0e-9)
    {
        draw_segment(rc, x1, y1, z1, x2, y2, z2);
        return;
    }

    included = 4.0 * atan(bulge);
    sagitta = (chord / 2.0) * bulge;
    perp_x = -dy / chord;
    perp_y = dx / chord;
    mid_x = (x1 + x2) / 2.0;
    mid_y = (y1 + y2) / 2.0;

    radius = chord / (2.0 * fabs(sin(included / 2.0)));
    cx = mid_x + perp_x * (radius - sagitta);
    cy = mid_y + perp_y * (radius - sagitta);
    start_angle = atan2(y1 - cy, x1 - cx);
    end_angle = atan2(y2 - cy, x2 - cx);

    sweep = end_angle - start_angle;
    /* fmod-based, O(1) normalization -- same fix, same reasoning, as
       draw_arc's own sweep normalization (see its comment) instead of
       an unbounded while loop. */
    sweep = fmod(sweep, 2.0 * M_PI);
    if (bulge > 0.0) { if (sweep <= 0.0) sweep += 2.0 * M_PI; }
    else             { if (sweep >= 0.0) sweep -= 2.0 * M_PI; }

    prev_x = x1; prev_y = y1;
    {
        double prev_z = z1;
        for (s = 1UL; s <= DWG_POLYLINE_ARC_SEGMENTS; s++)
        {
            double frac = (double)s / (double)DWG_POLYLINE_ARC_SEGMENTS;
            double a = start_angle + sweep * frac;
            double x = cx + radius * cos(a);
            double y = cy + radius * sin(a);
            double z = z1 + (z2 - z1) * frac; /* interpolacion lineal de Z a lo largo del arco */
            draw_segment(rc, prev_x, prev_y, prev_z, x, y, z);
            prev_x = x; prev_y = y; prev_z = z;
        }
    }
}

/* Arc/circle segment count: enough to look smooth without wasting time
   on tiny arcs -- 72 segments for a full circle (5 degrees each),
   scaled down proportionally for a shorter sweep, floor of 8. */
/* Real, confirmed hang found via this exact gap: this cast trusted
   sweep_rad to already be a small, sane value, with no bound of its
   own -- fine as long as every caller validates its angle inputs
   first (both real decode_arc's now do, after Arturo hit a real
   hang: "se cuelga"), but casting an out-of-range double to unsigned
   long is undefined behavior in C, and on this compiler/runtime it
   produced a garbage-huge count rather than clamping or trapping, so
   a single bad ARC could send draw_arc's loop into billions of
   iterations. Bounding here too (not just at the decode-time source)
   means this function can never be handed a future unvalidated angle
   -- from a new entity type, a different file format's reader, or a
   fix that regresses -- and still stay safe. */
#define DWG_RENDER_MAX_ARC_SEGMENTS 4096UL

static unsigned long segment_count_for_sweep(double sweep_rad)
{
    double frac, count_d;
    unsigned long n;

    if (!(sweep_rad > -1.0e6 && sweep_rad < 1.0e6)) /* NaN-safe: false for NaN too */
        return 8UL;

    frac = sweep_rad / (2.0 * M_PI);
    count_d = frac * 72.0 + 0.5;
    if (count_d < 8.0)
        return 8UL;
    if (count_d > (double)DWG_RENDER_MAX_ARC_SEGMENTS)
        return DWG_RENDER_MAX_ARC_SEGMENTS;

    n = (unsigned long)count_d;
    return n < 8UL ? 8UL : n;
}

static void draw_line(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_LINE3D *g = (DWG_LINE3D *)e->geometry;
    if (g == NULL) return;
    draw_segment(rc, g->start.x, g->start.y, g->start.z, g->end.x, g->end.y, g->end.z);
}

static void draw_circle(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_CIRCLE3D *g = (DWG_CIRCLE3D *)e->geometry;
    unsigned long i, n;
    double prev_x = 0.0, prev_y = 0.0;
    if (g == NULL) return;
    n = segment_count_for_sweep(2.0 * M_PI);
    for (i = 0UL; i <= n; i++)
    {
        double a = (2.0 * M_PI) * ((double)i / (double)n);
        double x = g->center.x + g->radius * cos(a);
        double y = g->center.y + g->radius * sin(a);
        if (i > 0UL)
            draw_segment(rc, prev_x, prev_y, g->center.z, x, y, g->center.z);
        prev_x = x; prev_y = y;
    }
}

static void draw_arc(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_ARC3D *g = (DWG_ARC3D *)e->geometry;
    unsigned long i, n;
    double start_rad, end_rad, sweep;
    double prev_x = 0.0, prev_y = 0.0;
    if (g == NULL) return;

    start_rad = g->start_angle * M_PI / 180.0;
    end_rad = g->end_angle * M_PI / 180.0;
    sweep = end_rad - start_rad;
    /* Real, confirmed hang found via this exact loop: for a wildly
       out-of-range sweep (an unvalidated garbage angle -- see
       decode_arc's own comment for how one could get this far) this
       could take an astronomical number of iterations to walk back
       into range one 2*M_PI step at a time. fmod is O(1) regardless
       of magnitude and NaN-safe (fmod of NaN is NaN, which then fails
       the <= 0.0 check and falls through unchanged, same as any other
       already-in-range value -- no worse than before for that case). */
    sweep = fmod(sweep, 2.0 * M_PI);
    if (sweep <= 0.0) sweep += 2.0 * M_PI; /* DWG arcs always sweep counterclockwise, start->end */

    n = segment_count_for_sweep(sweep);
    for (i = 0UL; i <= n; i++)
    {
        double a = start_rad + sweep * ((double)i / (double)n);
        double x = g->center.x + g->radius * cos(a);
        double y = g->center.y + g->radius * sin(a);
        if (i > 0UL)
            draw_segment(rc, prev_x, prev_y, g->center.z, x, y, g->center.z);
        prev_x = x; prev_y = y;
    }
}

static void draw_point(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_POINT3D *g = (DWG_POINT3D *)e->geometry;
    LONG px, py;
    if (g == NULL) return;
    world_to_pixel(rc, g->x, g->y, g->z, &px, &py);
    MoveToEx(rc->hdc, px - 3, py, NULL);
    LineTo(rc->hdc, px + 4, py);
    MoveToEx(rc->hdc, px, py - 3, NULL);
    LineTo(rc->hdc, px, py + 4);
}

static void draw_polyline(const DWG_RENDER_CTX *rc, HENTITY e)
{
    HPOLYLINE pl = dwg_polyline_from_entity(e);
    HVERTEX v, first;
    double prev_x = 0.0, prev_y = 0.0, prev_z = 0.0, prev_bulge = 0.0;
    int have_prev = 0;
    double fx = 0.0, fy = 0.0, fz = 0.0;
    if (pl == NULL) return;

    for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
    {
        double x, y, z, bulge;
        dwg_vertex_get_point(v, &x, &y, &z);
        bulge = dwg_vertex_get_bulge(v);
        if (have_prev)
            draw_bulge_segment(rc, prev_x, prev_y, prev_z, x, y, z, prev_bulge);
        else
        {
            fx = x; fy = y; fz = z;
        }
        prev_x = x; prev_y = y; prev_z = z; prev_bulge = bulge;
        have_prev = 1;
    }
    if (dwg_polyline_is_closed(pl) && have_prev)
        draw_bulge_segment(rc, prev_x, prev_y, prev_z, fx, fy, fz, prev_bulge);

    first = dwg_polyline_first_vertex(pl); /* silence unused-var warning on some paths */
    (void)first;
}

static void draw_solid(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_SOLID3D *g = (DWG_SOLID3D *)e->geometry;
    POINT pts[4];
    if (g == NULL) return;
    /* DXF/DWG SOLID winding is p1-p2-p4-p3 (a real quirk -- p3/p4 are
       swapped relative to a naive quad walk, otherwise this bowties). */
    world_to_pixel(rc, g->p1.x, g->p1.y, g->p1.z, &pts[0].x, &pts[0].y);
    world_to_pixel(rc, g->p2.x, g->p2.y, g->p2.z, &pts[1].x, &pts[1].y);
    world_to_pixel(rc, g->p4.x, g->p4.y, g->p4.z, &pts[2].x, &pts[2].y);
    world_to_pixel(rc, g->p3.x, g->p3.y, g->p3.z, &pts[3].x, &pts[3].y);
    Polygon(rc->hdc, pts, 4);
}

/*
 * 3DFACE: dibuja las 4 aristas como wireframe (SIN relleno -- el
 * ordenamiento de profundidad para caras rellenas queda para una vuelta
 * futura, ver plan de navegacion 3D). edge_flags respeta el bit-por-
 * arista de invisibilidad (DXF 70: 1=arista1,2=arista2,4=arista3,
 * 8=arista4) para no mostrar las diagonales internas de triangulacion
 * que AutoCAD agrega cuando una cara es en realidad un triangulo
 * (point4==point3) partido en dos.
 */
static void draw_face(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_FACE3D *g = (DWG_FACE3D *)e->geometry;
    unsigned short flags;
    if (g == NULL) return;
    flags = dwg_face_get_edge_flags(e);

    if (!(flags & 1U)) draw_segment(rc, g->p1.x, g->p1.y, g->p1.z, g->p2.x, g->p2.y, g->p2.z);
    if (!(flags & 2U)) draw_segment(rc, g->p2.x, g->p2.y, g->p2.z, g->p3.x, g->p3.y, g->p3.z);
    if (!(flags & 4U)) draw_segment(rc, g->p3.x, g->p3.y, g->p3.z, g->p4.x, g->p4.y, g->p4.z);
    if (!(flags & 8U)) draw_segment(rc, g->p4.x, g->p4.y, g->p4.z, g->p1.x, g->p1.y, g->p1.z);
}

/*
 * Fill pattern (crosshatch line spacing etc.) isn't modeled -- every
 * HATCH renders as a solid fill of its boundary loop, same
 * simplification draw_solid already makes for SOLID entities. Matches
 * a real gap Arturo found: ROTATORIO.dxf's colored pie-wedge fill
 * (a HATCH) rendered as nothing at all before this, since dwg_dxf_reader.c
 * already parsed HATCH but dwg_render_to_hdc never drew it.
 */
/* Same verified bulge-to-arc math as dwg_transform.c's own (private,
   file-local there) bulge_to_arc -- duplicated here rather than shared
   since that one's static to its own module; see dwg_transform.c for
   the full derivation/verification notes. bulge = tan(included_angle/4). */
#define DWG_HATCH_ARC_SEGMENTS 16UL

static void append_bulge_arc(const DWG_RENDER_CTX *rc, POINT *pts, unsigned long *pi,
                             double x1, double y1, double z1, double x2, double y2, double z2, double bulge)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double chord = sqrt(dx * dx + dy * dy);
    double included, sagitta, perp_x, perp_y, mid_x, mid_y;
    double cx, cy, radius, start_angle, end_angle, sweep;
    unsigned long s;

    if (chord < 1.0e-9)
        return;

    included = 4.0 * atan(bulge);
    sagitta = (chord / 2.0) * bulge;
    perp_x = -dy / chord;
    perp_y = dx / chord;
    mid_x = (x1 + x2) / 2.0;
    mid_y = (y1 + y2) / 2.0;

    radius = chord / (2.0 * fabs(sin(included / 2.0)));
    cx = mid_x + perp_x * (radius - sagitta);
    cy = mid_y + perp_y * (radius - sagitta);
    start_angle = atan2(y1 - cy, x1 - cx);
    end_angle = atan2(y2 - cy, x2 - cx);

    sweep = end_angle - start_angle;
    /* fmod-based, O(1) normalization -- same defensive fix as
       draw_arc's/draw_bulge_segment's own sweep normalization,
       applied here too rather than leaving this one unbounded loop
       as the odd one out. */
    sweep = fmod(sweep, 2.0 * M_PI);
    if (bulge > 0.0) { if (sweep <= 0.0) sweep += 2.0 * M_PI; }
    else             { if (sweep >= 0.0) sweep -= 2.0 * M_PI; }

    for (s = 1UL; s < DWG_HATCH_ARC_SEGMENTS; s++)
    {
        double frac = (double)s / (double)DWG_HATCH_ARC_SEGMENTS;
        double a = start_angle + sweep * frac;
        double x = cx + radius * cos(a);
        double y = cy + radius * sin(a);
        double z = z1 + (z2 - z1) * frac;
        world_to_pixel(rc, x, y, z, &pts[*pi].x, &pts[*pi].y);
        (*pi)++;
    }
}

static void draw_hatch(const DWG_RENDER_CTX *rc, HENTITY e)
{
    HVERTEX v;
    unsigned long count, i;
    POINT *pts;
    double prev_x = 0.0, prev_y = 0.0, prev_z = 0.0, prev_bulge = 0.0;
    int have_prev = 0;

    count = dwg_hatch_boundary_count(e);
    if (count < 3UL)
        return;

    /* generous upper bound: every vertex could have a curved segment
       to the next, each tessellated into DWG_HATCH_ARC_SEGMENTS-1 extra
       points (falta el hatch arco -- Arturo found the curved boundary
       was rendering as a straight chord). */
    pts = (POINT *)malloc(count * DWG_HATCH_ARC_SEGMENTS * sizeof(POINT));
    if (pts == NULL)
        return;

    i = 0UL;
    for (v = dwg_hatch_first_boundary_point(e); v != NULL; v = dwg_hatch_next_boundary_point(v))
    {
        double x, y, z, bulge;
        dwg_vertex_get_point(v, &x, &y, &z);
        bulge = dwg_vertex_get_bulge(v);

        if (have_prev && prev_bulge != 0.0)
            append_bulge_arc(rc, pts, &i, prev_x, prev_y, prev_z, x, y, z, prev_bulge);

        world_to_pixel(rc, x, y, z, &pts[i].x, &pts[i].y);
        i++;

        have_prev = 1;
        prev_x = x; prev_y = y; prev_z = z; prev_bulge = bulge;
    }

    /* closing segment (last vertex -> first vertex): Polygon() draws
       this edge automatically, but only as a straight line -- tessellate
       it here too if the last vertex's own bulge says it should curve. */
    if (have_prev && prev_bulge != 0.0)
    {
        HVERTEX first_v = dwg_hatch_first_boundary_point(e);
        double fx, fy, fz;
        if (first_v != NULL)
        {
            dwg_vertex_get_point(first_v, &fx, &fy, &fz);
            append_bulge_arc(rc, pts, &i, prev_x, prev_y, prev_z, fx, fy, fz, prev_bulge);
        }
    }

    if (i >= 3UL)
        Polygon(rc->hdc, pts, (int)i);

    free(pts);
}

/*
 * valign/halign are real GDI SetTextAlign flags (TA_LEFT/TA_CENTER/
 * TA_RIGHT, TA_TOP/TA_BOTTOM/TA_BASELINE) -- letting GDI itself place
 * the glyphs relative to (x,y) instead of always drawing top-left,
 * which is what this function did before and is wrong for TEXT's own
 * real AutoCAD convention (baseline-left for default justification):
 * with the previous unconditional TA_TOP|TA_LEFT default, (x,y) landed
 * at the TOP of the glyph cell, so every real baseline-anchored TEXT
 * rendered visibly shifted down by roughly one text-height -- confirmed
 * by Arturo after this file's binary TEXT/MTEXT support first went in
 * ("los numeros se leen pero estan desplazados").
 */
/*
 * Resuelve la fuente REAL de un TEXT via su estilo (pedido de Arturo
 * 2026-08-26, "agregar funcion para escribir texto definiendo el tipo
 * de letra y tamaño") -- BUG REAL encontrado de paso: draw_text_string
 * tenia "Arial" literal, sin importar el estilo real de la entidad, asi
 * que la tabla STYLE (dwg_style.h, con font_name ya modelado y get/set
 * completos desde el reverse original) nunca se consultaba para nada.
 * Sin style_name o sin encontrar el estilo o con font_name vacio, cae a
 * "Arial" -- mismo comportamiento de siempre para cualquier TEXT sin
 * fuente propia asignada, no rompe nada de lo que ya andaba.
 */
static const char *resolve_text_font(HDWG hDwg, HENTITY e)
{
    const char *style_name;
    HSTYLE style;
    const char *font;

    if (hDwg == NULL)
        return "Arial";

    style_name = dwg_text_get_style_name(e);
    if (style_name == NULL || style_name[0] == '\0')
        return "Arial";

    style = dwg_document_get_style(hDwg, style_name);
    if (style == NULL)
        return "Arial";

    font = dwg_style_get_font(style);
    if (font == NULL || font[0] == '\0')
        return "Arial";

    return font;
}

static void draw_text_string(const DWG_RENDER_CTX *rc, double x, double y, double z, double angle_deg,
                             double height_world, const char *text, UINT valign, UINT halign,
                             const char *font_name)
{
    LONG px, py;
    LONG height_px;
    HFONT hFont, hOldFont;
    int old_bk;
    UINT old_align;

    if (text == NULL || text[0] == '\0')
        return;

    if (font_name == NULL || font_name[0] == '\0')
        font_name = "Arial";

    world_to_pixel(rc, x, y, z, &px, &py);
    height_px = (LONG)(height_world / rc->scale + 0.5);
    if (height_px < 1) height_px = 1;

    /* NOT YET VISUALLY VERIFIED: GDI's lfEscapement is documented as
       "counterclockwise from the x-axis" but real-world behavior under
       a manually Y-flipped mapping (our case -- we compute device
       pixels ourselves per point, GDI never sees our world coordinate
       system) is a known source of sign confusion. Passing angle_deg
       directly here is a reasonable first guess, not a confirmed
       result -- check real rotated text on screen once the viewer is
       actually running, and negate here if it spins the wrong way. */
    hFont = CreateFontA(-height_px, 0, (LONG)(angle_deg * 10.0 + 0.5), (LONG)(angle_deg * 10.0 + 0.5),
                        FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_name);
    hOldFont = (HFONT)SelectObject(rc->hdc, hFont);
    old_bk = SetBkMode(rc->hdc, TRANSPARENT);
    old_align = SetTextAlign(rc->hdc, valign | halign);

    TextOutA(rc->hdc, px, py, text, (int)lstrlenA(text));

    SetTextAlign(rc->hdc, old_align);
    SetBkMode(rc->hdc, old_bk);
    SelectObject(rc->hdc, hOldFont);
    DeleteObject(hFont);
}

static void draw_text(const DWG_RENDER_CTX *rc, HENTITY e)
{
    double x, y, z;
    dwg_text_get_point(e, &x, &y, &z);
    draw_text_string(rc, x, y, z, dwg_text_get_angle(e), dwg_text_get_height(e), dwg_text_get_text(e),
                     TA_BASELINE, TA_LEFT, resolve_text_font(rc->hDwg, e));
}

/*
 * MTEXT's insertion point meaning depends on `attachment` (1-9, a
 * top/middle/bottom x left/center/right grid -- the same convention
 * AutoCAD's own DXF group 71 uses) -- stored by the R2004+ decoder but,
 * until now, never actually read back here, so every MTEXT rendered as
 * if attachment was always 1 (TopLeft). Horizontal alignment maps
 * directly to GDI's TA_LEFT/CENTER/RIGHT; GDI has no TA_MIDDLE for
 * vertical centering, so middle/bottom rows shift the anchor point in
 * WORLD space by height_world (half for middle, full for bottom) before
 * handing off to TA_TOP -- a single-line approximation (this engine
 * doesn't wrap/measure MTEXT's real multi-line layout), same scope as
 * MTEXT's other known gaps.
 */
static void draw_mtext(const DWG_RENDER_CTX *rc, HENTITY e)
{
    double x, y, z, height_world;
    unsigned short attach;
    UINT halign, valign = TA_TOP;

    dwg_mtext_get_point(e, &x, &y, &z);
    height_world = dwg_mtext_get_height(e);
    attach = dwg_mtext_get_attach(e);

    switch (attach)
    {
    case 2: case 5: case 8: halign = TA_CENTER; break;
    case 3: case 6: case 9: halign = TA_RIGHT;  break;
    default:                halign = TA_LEFT;   break;
    }
    switch (attach)
    {
    case 4: case 5: case 6: y -= height_world * 0.5; break; /* middle row */
    case 7: case 8: case 9: y -= height_world;       break; /* bottom row */
    default: break; /* top row: anchor as-is */
    }

    /* MTEXT no tiene un campo de estilo/fuente propio modelado todavia
       (a diferencia de TEXT, ver resolve_text_font arriba) -- sigue en
       "Arial" fijo, fuera de alcance esta vuelta (documentado, no
       accidental). */
    draw_text_string(rc, x, y, z, 0.0, height_world, dwg_mtext_get_text(e), valign, halign, "Arial");
}

/* Tamaño fijo en unidades de mundo para flechas/texto de cota -- NO
   consume la tabla DIMSTYLE ya armada (dwg_dimstyle.h) pero inerte,
   simplificacion deliberada de esta vuelta (pedido de Arturo
   2026-08-26, "permitir colocar cotas" -- ver el plan). */
#define DWG_DIM_ARROW_LEN   2.5
#define DWG_DIM_TEXT_HEIGHT 2.5

/* Flecha rellena de una cota: la punta toca (tipx,tipy) y la base se
   abre hacia adentro en la direccion (dirx,diry) -- mismo primitivo
   Polygon() que draw_solid ya usa para un relleno. */
static void draw_dimension_arrow(const DWG_RENDER_CTX *rc, double tipx, double tipy, double z,
                                 double dirx, double diry)
{
    double basex = tipx + dirx * DWG_DIM_ARROW_LEN;
    double basey = tipy + diry * DWG_DIM_ARROW_LEN;
    double perpx = -diry, perpy = dirx;
    double halfw = DWG_DIM_ARROW_LEN * 0.3;
    POINT pts[3];

    world_to_pixel(rc, tipx, tipy, z, &pts[0].x, &pts[0].y);
    world_to_pixel(rc, basex + perpx * halfw, basey + perpy * halfw, z, &pts[1].x, &pts[1].y);
    world_to_pixel(rc, basex - perpx * halfw, basey - perpy * halfw, z, &pts[2].x, &pts[2].y);
    Polygon(rc->hdc, pts, 3);
}

/*
 * Regenera las lineas de extension + la linea de cota via proyeccion
 * perpendicular de def_pt contra la direccion xline1->xline2 -- MISMA
 * formula ya escrita y verificada (Arturo la confirmo visualmente en
 * su momento) en bridge_dimension() de dwg_libredwg_bridge.c (lineas
 * 1246-1263), reusada tal cual aca (y otra vez en dwg_transform.c's
 * explode). El texto muestra Distancia(xline1,xline2) formateada con 2
 * decimales, centrado sobre la linea de cota.
 */
static void draw_dimension(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_DIMENSION3D *g = (DWG_DIMENSION3D *)e->geometry;
    double dx, dy, dist, dirx, diry, perpx, perpy;
    double def_perp, xl1_perp, xl2_perp, delta1, delta2;
    double dl1x, dl1y, dl2x, dl2y, midx, midy, angle_deg;
    char text[32];

    if (g == NULL) return;

    dx = g->xline2.x - g->xline1.x;
    dy = g->xline2.y - g->xline1.y;
    dist = sqrt(dx * dx + dy * dy);
    if (dist < 1.0e-9) { dirx = 1.0; diry = 0.0; } else { dirx = dx / dist; diry = dy / dist; }
    perpx = -diry; perpy = dirx;

    def_perp = g->def_pt.x * perpx + g->def_pt.y * perpy;
    xl1_perp = g->xline1.x * perpx + g->xline1.y * perpy;
    xl2_perp = g->xline2.x * perpx + g->xline2.y * perpy;
    delta1 = def_perp - xl1_perp;
    delta2 = def_perp - xl2_perp;
    dl1x = g->xline1.x + delta1 * perpx; dl1y = g->xline1.y + delta1 * perpy;
    dl2x = g->xline2.x + delta2 * perpx; dl2y = g->xline2.y + delta2 * perpy;

    draw_segment(rc, g->xline1.x, g->xline1.y, g->xline1.z, dl1x, dl1y, g->xline1.z);
    draw_segment(rc, g->xline2.x, g->xline2.y, g->xline2.z, dl2x, dl2y, g->xline2.z);
    draw_segment(rc, dl1x, dl1y, g->xline1.z, dl2x, dl2y, g->xline2.z);

    draw_dimension_arrow(rc, dl1x, dl1y, g->xline1.z, dirx, diry);
    draw_dimension_arrow(rc, dl2x, dl2y, g->xline2.z, -dirx, -diry);

    midx = (dl1x + dl2x) / 2.0;
    midy = (dl1y + dl2y) / 2.0;
    angle_deg = atan2(diry, dirx) * 180.0 / M_PI;

    sprintf(text, "%.2f", dist);
    draw_text_string(rc, midx, midy, g->xline1.z, angle_deg, DWG_DIM_TEXT_HEIGHT, text, TA_BOTTOM, TA_CENTER, "Arial");
}

/*
 * dwg_render.c nunca habia consultado el estado off/frozen de la capa
 * de una entidad (grep confirmado: cero referencias a dwg_layer_is_off/
 * is_frozen antes de esto) -- una capa apagada o congelada desde el
 * dialogo de Capas (dwg_layers_dlg.prg, pedido de Arturo 2026-08-26)
 * seguia dibujandose igual. Sin capa registrada para el nombre de la
 * entidad, visible por defecto (mismo comportamiento que siempre hubo).
 */
static int entity_layer_visible(HDWG hDwg, HENTITY e)
{
    HLAYER lay = dwg_document_get_layer(hDwg, dwg_entity_get_layer(e));

    if (lay == NULL)
        return 1;

    return !(dwg_layer_is_off(lay) || dwg_layer_is_frozen(lay));
}

void dwg_render_to_hdc(HDWG hDwg, void *hdc_v, long width, long height,
                       double scale, double origin_x, double origin_y,
                       const DWG_CAMERA3D *camera)
{
    HDC hdc = (HDC)hdc_v;
    DWG_RENDER_CTX rc;
    HENTITY e;
    HBRUSH bg_brush;
    RECT r;
    HPEN old_pen;
    HBRUSH old_brush;

    if (hDwg == NULL || hdc == NULL || scale <= 0.0)
        return;

    r.left = 0; r.top = 0; r.right = (LONG)width; r.bottom = (LONG)height;
    bg_brush = CreateSolidBrush((COLORREF)DWG_RENDER_BG_COLOR);
    FillRect(hdc, &r, bg_brush);
    DeleteObject(bg_brush);

    rc.hdc = hdc;
    rc.scale = scale;
    rc.origin_x = origin_x;
    rc.origin_y = origin_y;
    rc.camera = camera;
    rc.hDwg = hDwg;
    rc.pen_valid = 0;
    rc.pen_color = 0;
    rc.pen = NULL;
    rc.brush_valid = 0;
    rc.brush_color = 0;
    rc.brush = NULL;

    /* the HDC always has SOME pen/brush selected already (GDI stock
       objects) -- capture them so they can be restored at the end,
       same role the old fixed-white pen's old_pen played before
       per-entity color existed. */
    old_pen = (HPEN)SelectObject(hdc, GetStockObject(WHITE_PEN));
    old_brush = (HBRUSH)SelectObject(hdc, GetStockObject(WHITE_BRUSH));
    SetTextColor(hdc, RGB(255, 255, 255));

    /* REVERTIDO 2026-08-26: un relleno con ordenamiento pintor (segunda
       pasada de SOLID/FACE ordenada por profundidad de camara, con
       Polygon() relleno en vez de solo aristas) se probo y Arturo lo
       confirmo visualmente MAL ("no se ve correcto, deberia verse con
       estructura alambrica") -- con cientos de 3DFACE chicos (paredes/
       piso) cada uno con su propio color de capa, el resultado real fue
       una maraña de parches de color en vez de una estructura
       reconocible, muy distinto del wireframe simple ya confirmado
       funcionando antes de ese intento (ver memoria para el detalle
       completo, codigo removido del todo aca, no solo comentado). Un
       unico pase, FACE siempre en alambre (draw_face) sin importar el
       modo, igual que SOLID siempre relleno (asi era desde antes de
       toda esta sesion, sin cambios). */
    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        unsigned short color;

        if (!entity_layer_visible(hDwg, e))
            continue;

        /* seleccion actual (dwg_document_sel_contains) se resalta con un
           color fijo en vez del real de la entidad -- ver
           DWG_RENDER_SELECTED_PSEUDO_COLOR arriba */
        color = dwg_document_sel_contains(hDwg, e) ? DWG_RENDER_SELECTED_PSEUDO_COLOR : dwg_entity_get_color(e);
        set_pen_for_color(&rc, color);

        switch (dwg_entity_get_type(e))
        {
        case DWG_ENTITY_LINE:     draw_line(&rc, e); break;
        case DWG_ENTITY_CIRCLE:   draw_circle(&rc, e); break;
        case DWG_ENTITY_ARC:      draw_arc(&rc, e); break;
        case DWG_ENTITY_POINT:    draw_point(&rc, e); break;
        case DWG_ENTITY_POLYLINE: draw_polyline(&rc, e); break;
        case DWG_ENTITY_SOLID:    set_brush_for_color(&rc, color); draw_solid(&rc, e); break;
        case DWG_ENTITY_HATCH:    set_brush_for_color(&rc, color); draw_hatch(&rc, e); break;
        case DWG_ENTITY_TEXT:     draw_text(&rc, e); break;
        case DWG_ENTITY_MTEXT:    draw_mtext(&rc, e); break;
        case DWG_ENTITY_FACE:     draw_face(&rc, e); break;
        case DWG_ENTITY_DIMENSION: set_brush_for_color(&rc, color); draw_dimension(&rc, e); break;
        default: break; /* anything else: not modeled, see dwg_render.h */
        }
    }

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    if (rc.pen_valid)
        DeleteObject(rc.pen);
    if (rc.brush_valid)
        DeleteObject(rc.brush);
}

long dwg_render_get_extents(HDWG hDwg, double *min_x, double *min_y,
                            double *max_x, double *max_y)
{
    HENTITY e;
    int have_any = 0;
    double lo_x = 0.0, lo_y = 0.0, hi_x = 0.0, hi_y = 0.0;

#define DWG_RENDER_EXTEND(px, py) \
    do { \
        if (!have_any) { lo_x = hi_x = (px); lo_y = hi_y = (py); have_any = 1; } \
        else { \
            if ((px) < lo_x) lo_x = (px); if ((px) > hi_x) hi_x = (px); \
            if ((py) < lo_y) lo_y = (py); if ((py) > hi_y) hi_y = (py); \
        } \
    } while (0)

    if (hDwg == NULL)
        return 0L;

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        switch (dwg_entity_get_type(e))
        {
        case DWG_ENTITY_LINE:
        {
            DWG_LINE3D *g = (DWG_LINE3D *)e->geometry;
            if (g != NULL) { DWG_RENDER_EXTEND(g->start.x, g->start.y); DWG_RENDER_EXTEND(g->end.x, g->end.y); }
            break;
        }
        case DWG_ENTITY_CIRCLE:
        {
            DWG_CIRCLE3D *g = (DWG_CIRCLE3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND(g->center.x - g->radius, g->center.y - g->radius);
                DWG_RENDER_EXTEND(g->center.x + g->radius, g->center.y + g->radius);
            }
            break;
        }
        case DWG_ENTITY_ARC:
        {
            DWG_ARC3D *g = (DWG_ARC3D *)e->geometry;
            /* conservative: bound by the full circle rather than the
               real sweep -- correct, just not the tightest possible box */
            if (g != NULL)
            {
                DWG_RENDER_EXTEND(g->center.x - g->radius, g->center.y - g->radius);
                DWG_RENDER_EXTEND(g->center.x + g->radius, g->center.y + g->radius);
            }
            break;
        }
        case DWG_ENTITY_POINT:
        {
            DWG_POINT3D *g = (DWG_POINT3D *)e->geometry;
            if (g != NULL) DWG_RENDER_EXTEND(g->x, g->y);
            break;
        }
        case DWG_ENTITY_POLYLINE:
        {
            HPOLYLINE pl = dwg_polyline_from_entity(e);
            HVERTEX v;
            if (pl != NULL)
                for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
                {
                    double x, y, z;
                    dwg_vertex_get_point(v, &x, &y, &z);
                    DWG_RENDER_EXTEND(x, y);
                }
            break;
        }
        case DWG_ENTITY_SOLID:
        {
            DWG_SOLID3D *g = (DWG_SOLID3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND(g->p1.x, g->p1.y); DWG_RENDER_EXTEND(g->p2.x, g->p2.y);
                DWG_RENDER_EXTEND(g->p3.x, g->p3.y); DWG_RENDER_EXTEND(g->p4.x, g->p4.y);
            }
            break;
        }
        case DWG_ENTITY_TEXT:
        {
            double x, y, z;
            dwg_text_get_point(e, &x, &y, &z);
            DWG_RENDER_EXTEND(x, y);
            break;
        }
        case DWG_ENTITY_MTEXT:
        {
            double x, y, z;
            dwg_mtext_get_point(e, &x, &y, &z);
            DWG_RENDER_EXTEND(x, y);
            break;
        }
        case DWG_ENTITY_DIMENSION:
        {
            /* conservador: extiende por los 3 puntos crudos (xline1/
               xline2/def_pt), no por las lineas de cota derivadas --
               mismo criterio que ARC ya usa (bound by the full circle
               rather than the real sweep). */
            DWG_DIMENSION3D *g = (DWG_DIMENSION3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND(g->xline1.x, g->xline1.y);
                DWG_RENDER_EXTEND(g->xline2.x, g->xline2.y);
                DWG_RENDER_EXTEND(g->def_pt.x, g->def_pt.y);
            }
            break;
        }
        default: break;
        }
    }

#undef DWG_RENDER_EXTEND

    if (!have_any)
        return 0L;

    *min_x = lo_x; *min_y = lo_y; *max_x = hi_x; *max_y = hi_y;
    return 1L;
}

long dwg_render_get_extents_3d(HDWG hDwg, double *min_x, double *min_y, double *min_z,
                               double *max_x, double *max_y, double *max_z)
{
    HENTITY e;
    int have_any = 0;
    double lo_x = 0.0, lo_y = 0.0, lo_z = 0.0, hi_x = 0.0, hi_y = 0.0, hi_z = 0.0;

#define DWG_RENDER_EXTEND3(px, py, pz) \
    do { \
        if (!have_any) { lo_x = hi_x = (px); lo_y = hi_y = (py); lo_z = hi_z = (pz); have_any = 1; } \
        else { \
            if ((px) < lo_x) lo_x = (px); if ((px) > hi_x) hi_x = (px); \
            if ((py) < lo_y) lo_y = (py); if ((py) > hi_y) hi_y = (py); \
            if ((pz) < lo_z) lo_z = (pz); if ((pz) > hi_z) hi_z = (pz); \
        } \
    } while (0)

    if (hDwg == NULL)
        return 0L;

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        switch (dwg_entity_get_type(e))
        {
        case DWG_ENTITY_LINE:
        {
            DWG_LINE3D *g = (DWG_LINE3D *)e->geometry;
            if (g != NULL) { DWG_RENDER_EXTEND3(g->start.x, g->start.y, g->start.z); DWG_RENDER_EXTEND3(g->end.x, g->end.y, g->end.z); }
            break;
        }
        case DWG_ENTITY_CIRCLE:
        {
            DWG_CIRCLE3D *g = (DWG_CIRCLE3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND3(g->center.x - g->radius, g->center.y - g->radius, g->center.z);
                DWG_RENDER_EXTEND3(g->center.x + g->radius, g->center.y + g->radius, g->center.z);
            }
            break;
        }
        case DWG_ENTITY_ARC:
        {
            DWG_ARC3D *g = (DWG_ARC3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND3(g->center.x - g->radius, g->center.y - g->radius, g->center.z);
                DWG_RENDER_EXTEND3(g->center.x + g->radius, g->center.y + g->radius, g->center.z);
            }
            break;
        }
        case DWG_ENTITY_POINT:
        {
            DWG_POINT3D *g = (DWG_POINT3D *)e->geometry;
            if (g != NULL) DWG_RENDER_EXTEND3(g->x, g->y, g->z);
            break;
        }
        case DWG_ENTITY_POLYLINE:
        {
            HPOLYLINE pl = dwg_polyline_from_entity(e);
            HVERTEX v;
            if (pl != NULL)
                for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
                {
                    double x, y, z;
                    dwg_vertex_get_point(v, &x, &y, &z);
                    DWG_RENDER_EXTEND3(x, y, z);
                }
            break;
        }
        case DWG_ENTITY_SOLID:
        {
            DWG_SOLID3D *g = (DWG_SOLID3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND3(g->p1.x, g->p1.y, g->p1.z); DWG_RENDER_EXTEND3(g->p2.x, g->p2.y, g->p2.z);
                DWG_RENDER_EXTEND3(g->p3.x, g->p3.y, g->p3.z); DWG_RENDER_EXTEND3(g->p4.x, g->p4.y, g->p4.z);
            }
            break;
        }
        case DWG_ENTITY_FACE:
        {
            DWG_FACE3D *g = (DWG_FACE3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND3(g->p1.x, g->p1.y, g->p1.z); DWG_RENDER_EXTEND3(g->p2.x, g->p2.y, g->p2.z);
                DWG_RENDER_EXTEND3(g->p3.x, g->p3.y, g->p3.z); DWG_RENDER_EXTEND3(g->p4.x, g->p4.y, g->p4.z);
            }
            break;
        }
        case DWG_ENTITY_TEXT:
        {
            double x, y, z;
            dwg_text_get_point(e, &x, &y, &z);
            DWG_RENDER_EXTEND3(x, y, z);
            break;
        }
        case DWG_ENTITY_MTEXT:
        {
            double x, y, z;
            dwg_mtext_get_point(e, &x, &y, &z);
            DWG_RENDER_EXTEND3(x, y, z);
            break;
        }
        case DWG_ENTITY_DIMENSION:
        {
            DWG_DIMENSION3D *g = (DWG_DIMENSION3D *)e->geometry;
            if (g != NULL)
            {
                DWG_RENDER_EXTEND3(g->xline1.x, g->xline1.y, g->xline1.z);
                DWG_RENDER_EXTEND3(g->xline2.x, g->xline2.y, g->xline2.z);
                DWG_RENDER_EXTEND3(g->def_pt.x, g->def_pt.y, g->def_pt.z);
            }
            break;
        }
        default: break;
        }
    }

#undef DWG_RENDER_EXTEND3

    if (!have_any)
        return 0L;

    *min_x = lo_x; *min_y = lo_y; *min_z = lo_z;
    *max_x = hi_x; *max_y = hi_y; *max_z = hi_z;
    return 1L;
}
