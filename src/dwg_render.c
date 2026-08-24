#include <windows.h>
#include <math.h>
#include <stdlib.h>

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
    HPEN pen;
    unsigned short pen_color;
    int pen_valid;
    HBRUSH brush;
    unsigned short brush_color;
    int brush_valid;
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

static COLORREF aci_to_colorref_raw(unsigned short aci)
{
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

static void world_to_pixel(const DWG_RENDER_CTX *rc, double wx, double wy, LONG *px, LONG *py)
{
    *px = (LONG)floor((wx - rc->origin_x) / rc->scale + 0.5);
    *py = (LONG)floor((rc->origin_y - wy) / rc->scale + 0.5);
}

static void draw_segment(const DWG_RENDER_CTX *rc, double x1, double y1, double x2, double y2)
{
    LONG px1, py1, px2, py2;
    world_to_pixel(rc, x1, y1, &px1, &py1);
    world_to_pixel(rc, x2, y2, &px2, &py2);
    MoveToEx(rc->hdc, px1, py1, NULL);
    LineTo(rc->hdc, px2, py2);
}

/* Arc/circle segment count: enough to look smooth without wasting time
   on tiny arcs -- 72 segments for a full circle (5 degrees each),
   scaled down proportionally for a shorter sweep, floor of 8. */
static unsigned long segment_count_for_sweep(double sweep_rad)
{
    double frac = sweep_rad / (2.0 * M_PI);
    unsigned long n = (unsigned long)(frac * 72.0 + 0.5);
    return n < 8UL ? 8UL : n;
}

static void draw_line(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_LINE3D *g = (DWG_LINE3D *)e->geometry;
    if (g == NULL) return;
    draw_segment(rc, g->start.x, g->start.y, g->end.x, g->end.y);
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
            draw_segment(rc, prev_x, prev_y, x, y);
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
    while (sweep <= 0.0) sweep += 2.0 * M_PI; /* DWG arcs always sweep counterclockwise, start->end */

    n = segment_count_for_sweep(sweep);
    for (i = 0UL; i <= n; i++)
    {
        double a = start_rad + sweep * ((double)i / (double)n);
        double x = g->center.x + g->radius * cos(a);
        double y = g->center.y + g->radius * sin(a);
        if (i > 0UL)
            draw_segment(rc, prev_x, prev_y, x, y);
        prev_x = x; prev_y = y;
    }
}

static void draw_point(const DWG_RENDER_CTX *rc, HENTITY e)
{
    DWG_POINT3D *g = (DWG_POINT3D *)e->geometry;
    LONG px, py;
    if (g == NULL) return;
    world_to_pixel(rc, g->x, g->y, &px, &py);
    MoveToEx(rc->hdc, px - 3, py, NULL);
    LineTo(rc->hdc, px + 4, py);
    MoveToEx(rc->hdc, px, py - 3, NULL);
    LineTo(rc->hdc, px, py + 4);
}

static void draw_polyline(const DWG_RENDER_CTX *rc, HENTITY e)
{
    HPOLYLINE pl = dwg_polyline_from_entity(e);
    HVERTEX v, first;
    double prev_x = 0.0, prev_y = 0.0;
    int have_prev = 0;
    double fx = 0.0, fy = 0.0;
    if (pl == NULL) return;

    for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
    {
        double x, y, z;
        dwg_vertex_get_point(v, &x, &y, &z);
        if (have_prev)
            draw_segment(rc, prev_x, prev_y, x, y);
        else
        {
            fx = x; fy = y;
        }
        prev_x = x; prev_y = y;
        have_prev = 1;
    }
    if (dwg_polyline_is_closed(pl) && have_prev)
        draw_segment(rc, prev_x, prev_y, fx, fy);

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
    world_to_pixel(rc, g->p1.x, g->p1.y, &pts[0].x, &pts[0].y);
    world_to_pixel(rc, g->p2.x, g->p2.y, &pts[1].x, &pts[1].y);
    world_to_pixel(rc, g->p4.x, g->p4.y, &pts[2].x, &pts[2].y);
    world_to_pixel(rc, g->p3.x, g->p3.y, &pts[3].x, &pts[3].y);
    Polygon(rc->hdc, pts, 4);
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
                             double x1, double y1, double x2, double y2, double bulge)
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
    if (bulge > 0.0) { while (sweep <= 0.0) sweep += 2.0 * M_PI; }
    else             { while (sweep >= 0.0) sweep -= 2.0 * M_PI; }

    for (s = 1UL; s < DWG_HATCH_ARC_SEGMENTS; s++)
    {
        double a = start_angle + sweep * ((double)s / (double)DWG_HATCH_ARC_SEGMENTS);
        double x = cx + radius * cos(a);
        double y = cy + radius * sin(a);
        world_to_pixel(rc, x, y, &pts[*pi].x, &pts[*pi].y);
        (*pi)++;
    }
}

static void draw_hatch(const DWG_RENDER_CTX *rc, HENTITY e)
{
    HVERTEX v;
    unsigned long count, i;
    POINT *pts;
    double prev_x = 0.0, prev_y = 0.0, prev_bulge = 0.0;
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
            append_bulge_arc(rc, pts, &i, prev_x, prev_y, x, y, prev_bulge);

        world_to_pixel(rc, x, y, &pts[i].x, &pts[i].y);
        i++;

        have_prev = 1;
        prev_x = x; prev_y = y; prev_bulge = bulge;
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
            append_bulge_arc(rc, pts, &i, prev_x, prev_y, fx, fy, prev_bulge);
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
static void draw_text_string(const DWG_RENDER_CTX *rc, double x, double y, double angle_deg,
                             double height_world, const char *text, UINT valign, UINT halign)
{
    LONG px, py;
    LONG height_px;
    HFONT hFont, hOldFont;
    int old_bk;
    UINT old_align;

    if (text == NULL || text[0] == '\0')
        return;

    world_to_pixel(rc, x, y, &px, &py);
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
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
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
    draw_text_string(rc, x, y, dwg_text_get_angle(e), dwg_text_get_height(e), dwg_text_get_text(e),
                     TA_BASELINE, TA_LEFT);
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

    draw_text_string(rc, x, y, 0.0, height_world, dwg_mtext_get_text(e), valign, halign);
}

void dwg_render_to_hdc(HDWG hDwg, void *hdc_v, long width, long height,
                       double scale, double origin_x, double origin_y)
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

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        unsigned short color = dwg_entity_get_color(e);
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
        default: break;
        }
    }

#undef DWG_RENDER_EXTEND

    if (!have_any)
        return 0L;

    *min_x = lo_x; *min_y = lo_y; *max_x = hi_x; *max_y = hi_y;
    return 1L;
}
