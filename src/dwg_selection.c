#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dwg_selection.h"
#include "dwg_transform.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_hatch.h"
#include "dwg_leader.h"
#include "dwg_solid.h"
#include "dwg_insert.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double dist_points(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

static double dist_point_segment(double px, double py, double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    double t, cx, cy;

    if (len2 < 1e-12)
        return dist_points(px, py, x1, y1);

    t = ((px - x1) * dx + (py - y1) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    cx = x1 + t * dx;
    cy = y1 + t * dy;

    return dist_points(px, py, cx, cy);
}

static double norm360(double deg)
{
    while (deg < 0.0)
        deg += 360.0;
    while (deg >= 360.0)
        deg -= 360.0;
    return deg;
}

/* Same CCW-sweep-from-start_angle-to-end_angle convention as
   dwg_transform.c's angle_in_arc_sweep (duplicated locally rather than
   shared across translation units, matching this codebase's existing
   convention of keeping small per-file helpers static). */
static int angle_in_arc_sweep(double test_deg, double start_deg, double end_deg)
{
    double sweep = norm360(end_deg - start_deg);
    double rel = norm360(test_deg - start_deg);

    if (sweep < 1e-9)
        sweep = 360.0;

    return rel <= sweep + 1e-9;
}

static void bbox_include(double x, double y, double *xmin, double *ymin, double *xmax, double *ymax, int *valid)
{
    if (!*valid)
    {
        *xmin = *xmax = x;
        *ymin = *ymax = y;
        *valid = 1;
        return;
    }

    if (x < *xmin) *xmin = x;
    if (x > *xmax) *xmax = x;
    if (y < *ymin) *ymin = y;
    if (y > *ymax) *ymax = y;
}

/* Hit-test: is (px,py) within tolerance of hEntity's geometry? See the
   coverage/simplification notes in dwg_selection.h. */
static int entity_hit_test(HENTITY e, double px, double py, double tolerance)
{
    if (e == NULL || e->geometry == NULL)
        return 0;

    switch (e->type)
    {
    case DWG_ENTITY_POINT:
    {
        DWG_POINT3D *p = (DWG_POINT3D *)e->geometry;
        return dist_points(px, py, p->x, p->y) <= tolerance;
    }
    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)e->geometry;
        return dist_point_segment(px, py, l->start.x, l->start.y, l->end.x, l->end.y) <= tolerance;
    }
    case DWG_ENTITY_CIRCLE:
    {
        DWG_CIRCLE3D *c = (DWG_CIRCLE3D *)e->geometry;
        return fabs(dist_points(px, py, c->center.x, c->center.y) - c->radius) <= tolerance;
    }
    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)e->geometry;
        double d = dist_points(px, py, a->center.x, a->center.y);
        double ang;

        if (fabs(d - a->radius) > tolerance)
            return 0;

        ang = atan2(py - a->center.y, px - a->center.x) * 180.0 / M_PI;
        return angle_in_arc_sweep(ang, a->start_angle, a->end_angle);
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z;
        dwg_text_get_point(e, &x, &y, &z);
        return dist_points(px, py, x, y) <= tolerance;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        dwg_mtext_get_point(e, &x, &y, &z);
        return dist_points(px, py, x, y) <= tolerance;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z;
        dwg_insert_get_point(e, &x, &y, &z);
        return dist_points(px, py, x, y) <= tolerance;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(e);
        HVERTEX v, vnext;
        long closed = pl != NULL ? dwg_polyline_is_closed(pl) : 0;

        for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
        {
            double x1, y1, z1, x2, y2, z2;

            vnext = dwg_polyline_next_vertex(v);
            if (vnext == NULL)
            {
                if (!closed)
                    break;
                vnext = dwg_polyline_first_vertex(pl);
            }

            dwg_vertex_get_point(v, &x1, &y1, &z1);
            dwg_vertex_get_point(vnext, &x2, &y2, &z2);

            if (dist_point_segment(px, py, x1, y1, x2, y2) <= tolerance)
                return 1;
        }
        return 0;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX first = dwg_hatch_first_boundary_point(e);
        HVERTEX v, vnext;

        for (v = first; v != NULL; v = dwg_hatch_next_boundary_point(v))
        {
            double x1, y1, z1, x2, y2, z2;

            vnext = dwg_hatch_next_boundary_point(v);
            if (vnext == NULL)
                vnext = first; /* boundary is implicitly closed */

            dwg_vertex_get_point(v, &x1, &y1, &z1);
            dwg_vertex_get_point(vnext, &x2, &y2, &z2);

            if (dist_point_segment(px, py, x1, y1, x2, y2) <= tolerance)
                return 1;
        }
        return 0;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v, vnext;

        for (v = dwg_leader_first_vertex(e); v != NULL; v = dwg_leader_next_vertex(v))
        {
            double x1, y1, z1, x2, y2, z2;

            vnext = dwg_leader_next_vertex(v);
            if (vnext == NULL)
                break; /* leader is an open polyline, no closing segment */

            dwg_vertex_get_point(v, &x1, &y1, &z1);
            dwg_vertex_get_point(vnext, &x2, &y2, &z2);

            if (dist_point_segment(px, py, x1, y1, x2, y2) <= tolerance)
                return 1;
        }
        return 0;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)e->geometry;
        double xs[4], ys[4];
        int i;

        xs[0] = s->p1.x; ys[0] = s->p1.y;
        xs[1] = s->p2.x; ys[1] = s->p2.y;
        xs[2] = s->p3.x; ys[2] = s->p3.y;
        xs[3] = s->p4.x; ys[3] = s->p4.y;

        for (i = 0; i < 4; i++)
        {
            int j = (i + 1) % 4;
            if (dist_point_segment(px, py, xs[i], ys[i], xs[j], ys[j]) <= tolerance)
                return 1;
        }
        return 0;
    }
    default:
        return 0;
    }
}

/* Bounding box of hEntity's geometry. CIRCLE/ARC use center+-radius (a
   safe conservative box even for a partial ARC sweep -- see the
   crossing/window caveat in dwg_selection.h). Returns 0 (via *valid) if
   the type isn't covered or has no geometry, meaning it's excluded from
   window/crossing select entirely. */
static void entity_bbox(HENTITY e, double *xmin, double *ymin, double *xmax, double *ymax, int *valid)
{
    *valid = 0;

    if (e == NULL || e->geometry == NULL)
        return;

    switch (e->type)
    {
    case DWG_ENTITY_POINT:
    {
        DWG_POINT3D *p = (DWG_POINT3D *)e->geometry;
        bbox_include(p->x, p->y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)e->geometry;
        bbox_include(l->start.x, l->start.y, xmin, ymin, xmax, ymax, valid);
        bbox_include(l->end.x, l->end.y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_CIRCLE:
    {
        DWG_CIRCLE3D *c = (DWG_CIRCLE3D *)e->geometry;
        bbox_include(c->center.x - c->radius, c->center.y - c->radius, xmin, ymin, xmax, ymax, valid);
        bbox_include(c->center.x + c->radius, c->center.y + c->radius, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)e->geometry;
        bbox_include(a->center.x - a->radius, a->center.y - a->radius, xmin, ymin, xmax, ymax, valid);
        bbox_include(a->center.x + a->radius, a->center.y + a->radius, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z;
        dwg_text_get_point(e, &x, &y, &z);
        bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        dwg_mtext_get_point(e, &x, &y, &z);
        bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z;
        dwg_insert_get_point(e, &x, &y, &z);
        bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(e);
        HVERTEX v;
        for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v;
        for (v = dwg_hatch_first_boundary_point(e); v != NULL; v = dwg_hatch_next_boundary_point(v))
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v;
        for (v = dwg_leader_first_vertex(e); v != NULL; v = dwg_leader_next_vertex(v))
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            bbox_include(x, y, xmin, ymin, xmax, ymax, valid);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)e->geometry;
        bbox_include(s->p1.x, s->p1.y, xmin, ymin, xmax, ymax, valid);
        bbox_include(s->p2.x, s->p2.y, xmin, ymin, xmax, ymax, valid);
        bbox_include(s->p3.x, s->p3.y, xmin, ymin, xmax, ymax, valid);
        bbox_include(s->p4.x, s->p4.y, xmin, ymin, xmax, ymax, valid);
        break;
    }
    default:
        break;
    }
}

unsigned long dwg_select_point(HDWG hDwg, double px, double py, double tolerance)
{
    HENTITY e;
    unsigned long added = 0UL;

    if (hDwg == NULL)
        return 0UL;

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        if (entity_hit_test(e, px, py, tolerance))
        {
            if (!dwg_document_sel_contains(hDwg, e))
                added++;
            dwg_document_sel_add(hDwg, e);
        }
    }

    return added;
}

unsigned long dwg_select_window(HDWG hDwg, double x1, double y1, double x2, double y2, DWG_BOOL crossing)
{
    HENTITY e;
    unsigned long added = 0UL;
    double rxmin, rymin, rxmax, rymax;

    if (hDwg == NULL)
        return 0UL;

    rxmin = (x1 < x2) ? x1 : x2;
    rxmax = (x1 < x2) ? x2 : x1;
    rymin = (y1 < y2) ? y1 : y2;
    rymax = (y1 < y2) ? y2 : y1;

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        double exmin, eymin, exmax, eymax;
        int valid;
        int hit;

        entity_bbox(e, &exmin, &eymin, &exmax, &eymax, &valid);
        if (!valid)
            continue;

        if (crossing)
        {
            /* overlap test: NOT (entirely to one side) */
            hit = !(exmax < rxmin || exmin > rxmax || eymax < rymin || eymin > rymax);
        }
        else
        {
            hit = (exmin >= rxmin && exmax <= rxmax && eymin >= rymin && eymax <= rymax);
        }

        if (hit)
        {
            if (!dwg_document_sel_contains(hDwg, e))
                added++;
            dwg_document_sel_add(hDwg, e);
        }
    }

    return added;
}

unsigned long dwg_select_layer(HDWG hDwg, const char *layer_name)
{
    HENTITY e;
    unsigned long added = 0UL;

    if (hDwg == NULL || layer_name == NULL)
        return 0UL;

    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        if (strcmp(dwg_entity_get_layer(e), layer_name) == 0)
        {
            if (!dwg_document_sel_contains(hDwg, e))
                added++;
            dwg_document_sel_add(hDwg, e);
        }
    }

    return added;
}

void dwg_sel_move(HDWG hDwg, double dx, double dy, double dz)
{
    unsigned long i, count;

    if (hDwg == NULL)
        return;

    count = dwg_document_sel_count(hDwg);
    for (i = 0UL; i < count; i++)
        dwg_entity_move(dwg_document_sel_get(hDwg, i), dx, dy, dz);
}

void dwg_sel_rotate(HDWG hDwg, double cx, double cy, double cz, double angle_degrees)
{
    unsigned long i, count;

    if (hDwg == NULL)
        return;

    count = dwg_document_sel_count(hDwg);
    for (i = 0UL; i < count; i++)
        dwg_entity_rotate(dwg_document_sel_get(hDwg, i), cx, cy, cz, angle_degrees);
}

void dwg_sel_scale(HDWG hDwg, double cx, double cy, double cz, double factor)
{
    unsigned long i, count;

    if (hDwg == NULL)
        return;

    count = dwg_document_sel_count(hDwg);
    for (i = 0UL; i < count; i++)
        dwg_entity_scale(dwg_document_sel_get(hDwg, i), cx, cy, cz, factor);
}

void dwg_sel_mirror(HDWG hDwg, double x1, double y1, double x2, double y2)
{
    unsigned long i, count;

    if (hDwg == NULL)
        return;

    count = dwg_document_sel_count(hDwg);
    for (i = 0UL; i < count; i++)
        dwg_entity_mirror(dwg_document_sel_get(hDwg, i), x1, y1, x2, y2);
}

unsigned long dwg_sel_erase(HDWG hDwg)
{
    unsigned long i, count, erased = 0UL;
    HENTITY *items;

    if (hDwg == NULL)
        return 0UL;

    count = dwg_document_sel_count(hDwg);
    if (count == 0UL)
        return 0UL;

    /* dwg_document_remove_entity purges the entity it destroys from the
       selection array as it goes (see dwg_document_sel_purge in
       dwg_document.c, a swap-with-last removal), which would otherwise
       shift indices out from under a live dwg_document_sel_get(hDwg, i)
       loop and skip entries -- snapshot the pointers first, same
       approach dwg_sel_copy uses for the same reason. */
    items = (HENTITY *)malloc((size_t)count * sizeof(HENTITY));
    if (items == NULL)
        return 0UL;

    for (i = 0UL; i < count; i++)
        items[i] = dwg_document_sel_get(hDwg, i);

    for (i = 0UL; i < count; i++)
    {
        if (items[i] != NULL && dwg_document_remove_entity(hDwg, items[i]))
            erased++;
    }

    free(items);

    dwg_document_sel_clear(hDwg);

    return erased;
}

unsigned long dwg_sel_explode(HDWG hDwg)
{
    unsigned long i, count, total = 0UL;

    if (hDwg == NULL)
        return 0UL;

    count = dwg_document_sel_count(hDwg);
    for (i = 0UL; i < count; i++)
        total += dwg_entity_explode(hDwg, dwg_document_sel_get(hDwg, i));

    return total;
}

typedef struct
{
    HENTITY entity;
    unsigned long orig_index; /* position of 'entity' in dwg_sel_join's orig[] snapshot */
    double x1, y1, z1;
    double x2, y2, z2;
    double bulge; /* forward (p1->p2) bulge; 0 for LINE */
    int used;
} DWG_JOIN_ITEM;

static int points_close(double x1, double y1, double x2, double y2, double tolerance)
{
    return dist_points(x1, y1, x2, y2) <= tolerance;
}

/* Builds a JOIN_ITEM for a LINE or eligible ARC (non-degenerate sweep,
   i.e. not ~0 and not ~360 degrees, since the bulge for those is
   undefined/infinite). Returns 1 and fills *out on success (out->used
   and out->orig_index are left for the caller to set), 0 if e isn't a
   joinable type or is a degenerate ARC. */
static int join_item_from_entity(HENTITY e, DWG_JOIN_ITEM *out)
{
    if (e == NULL || e->geometry == NULL)
        return 0;

    if (e->type == DWG_ENTITY_LINE)
    {
        DWG_LINE3D *l = (DWG_LINE3D *)e->geometry;
        out->entity = e;
        out->x1 = l->start.x; out->y1 = l->start.y; out->z1 = l->start.z;
        out->x2 = l->end.x;   out->y2 = l->end.y;   out->z2 = l->end.z;
        out->bulge = 0.0;
        return 1;
    }

    if (e->type == DWG_ENTITY_ARC)
    {
        DWG_ARC3D *a = (DWG_ARC3D *)e->geometry;
        double sweep = norm360(a->end_angle - a->start_angle);
        double start_rad, end_rad;

        if (sweep < 1e-6 || sweep > 360.0 - 1e-6)
            return 0; /* degenerate: bulge undefined for a ~0 or ~360 degree sweep */

        start_rad = a->start_angle * M_PI / 180.0;
        end_rad = a->end_angle * M_PI / 180.0;

        out->entity = e;
        out->x1 = a->center.x + a->radius * cos(start_rad);
        out->y1 = a->center.y + a->radius * sin(start_rad);
        out->z1 = a->center.z;
        out->x2 = a->center.x + a->radius * cos(end_rad);
        out->y2 = a->center.y + a->radius * sin(end_rad);
        out->z2 = a->center.z;
        out->bulge = tan((sweep * M_PI / 180.0) / 4.0);
        return 1;
    }

    return 0;
}

unsigned long dwg_sel_join(HDWG hDwg, double tolerance)
{
    unsigned long orig_count, i;
    HENTITY *orig;
    int *erased;
    DWG_JOIN_ITEM *items;
    unsigned long item_count = 0UL;
    unsigned long *chain_idx, *back_idx;
    int *chain_rev, *back_rev;
    HENTITY *created;
    unsigned long created_count = 0UL;

    if (hDwg == NULL)
        return 0UL;

    orig_count = dwg_document_sel_count(hDwg);
    if (orig_count == 0UL)
        return 0UL;

    orig = (HENTITY *)malloc((size_t)orig_count * sizeof(HENTITY));
    erased = (int *)calloc((size_t)orig_count, sizeof(int));
    items = (DWG_JOIN_ITEM *)malloc((size_t)orig_count * sizeof(DWG_JOIN_ITEM));
    chain_idx = (unsigned long *)malloc((size_t)orig_count * sizeof(unsigned long));
    back_idx = (unsigned long *)malloc((size_t)orig_count * sizeof(unsigned long));
    chain_rev = (int *)malloc((size_t)orig_count * sizeof(int));
    back_rev = (int *)malloc((size_t)orig_count * sizeof(int));
    created = (HENTITY *)malloc((size_t)orig_count * sizeof(HENTITY));

    if (orig == NULL || erased == NULL || items == NULL || chain_idx == NULL || back_idx == NULL ||
        chain_rev == NULL || back_rev == NULL || created == NULL)
    {
        if (orig != NULL) free(orig);
        if (erased != NULL) free(erased);
        if (items != NULL) free(items);
        if (chain_idx != NULL) free(chain_idx);
        if (back_idx != NULL) free(back_idx);
        if (chain_rev != NULL) free(chain_rev);
        if (back_rev != NULL) free(back_rev);
        if (created != NULL) free(created);
        return 0UL;
    }

    for (i = 0UL; i < orig_count; i++)
        orig[i] = dwg_document_sel_get(hDwg, i);

    for (i = 0UL; i < orig_count; i++)
    {
        DWG_JOIN_ITEM item;
        if (join_item_from_entity(orig[i], &item))
        {
            item.used = 0;
            item.orig_index = i;
            items[item_count] = item;
            item_count++;
        }
    }

    for (i = 0UL; i < item_count; i++)
    {
        unsigned long chain_len, back_len, total, k;
        double chain_start_x, chain_start_y, chain_end_x, chain_end_y;
        HENTITY new_pl;
        HPOLYLINE hpl;

        if (items[i].used)
            continue;

        items[i].used = 1;
        chain_idx[0] = i; chain_rev[0] = 0;
        chain_len = 1UL;
        chain_start_x = items[i].x1; chain_start_y = items[i].y1;
        chain_end_x = items[i].x2; chain_end_y = items[i].y2;

        /* grow forward from chain_end */
        for (;;)
        {
            long found = -1L;
            int found_rev = 0;
            unsigned long j;

            for (j = 0UL; j < item_count; j++)
            {
                if (items[j].used) continue;
                if (points_close(items[j].x1, items[j].y1, chain_end_x, chain_end_y, tolerance))
                { found = (long)j; found_rev = 0; break; }
                if (points_close(items[j].x2, items[j].y2, chain_end_x, chain_end_y, tolerance))
                { found = (long)j; found_rev = 1; break; }
            }
            if (found < 0L) break;

            items[found].used = 1;
            chain_idx[chain_len] = (unsigned long)found;
            chain_rev[chain_len] = found_rev;
            chain_len++;
            chain_end_x = found_rev ? items[found].x1 : items[found].x2;
            chain_end_y = found_rev ? items[found].y1 : items[found].y2;
        }

        /* grow backward from chain_start */
        back_len = 0UL;
        for (;;)
        {
            long found = -1L;
            int found_rev = 0;
            unsigned long j;

            for (j = 0UL; j < item_count; j++)
            {
                if (items[j].used) continue;
                if (points_close(items[j].x2, items[j].y2, chain_start_x, chain_start_y, tolerance))
                { found = (long)j; found_rev = 0; break; }
                if (points_close(items[j].x1, items[j].y1, chain_start_x, chain_start_y, tolerance))
                { found = (long)j; found_rev = 1; break; }
            }
            if (found < 0L) break;

            items[found].used = 1;
            back_idx[back_len] = (unsigned long)found;
            back_rev[back_len] = found_rev;
            back_len++;
            chain_start_x = found_rev ? items[found].x2 : items[found].x1;
            chain_start_y = found_rev ? items[found].y2 : items[found].y1;
        }

        total = back_len + chain_len;
        if (total < 2UL)
            continue; /* singleton: leave it selected, untouched, unmerged */

        new_pl = dwg_add_polyline(hDwg);
        hpl = dwg_polyline_from_entity(new_pl);
        if (new_pl == NULL || hpl == NULL)
            continue;

        {
            long closed = points_close(chain_start_x, chain_start_y, chain_end_x, chain_end_y, tolerance);

            for (k = 0UL; k < total; k++)
            {
                unsigned long idx;
                int rev;
                double sx, sy, sz, bulge;
                int is_last = (k == total - 1UL);

                if (k < back_len)
                {
                    idx = back_idx[back_len - 1UL - k];
                    rev = back_rev[back_len - 1UL - k];
                }
                else
                {
                    idx = chain_idx[k - back_len];
                    rev = chain_rev[k - back_len];
                }

                sx = rev ? items[idx].x2 : items[idx].x1;
                sy = rev ? items[idx].y2 : items[idx].y1;
                sz = rev ? items[idx].z2 : items[idx].z1;
                bulge = rev ? -items[idx].bulge : items[idx].bulge;

                dwg_polyline_add_vertex2(hpl, sx, sy, sz, bulge, 0.0, 0.0);

                if (is_last && !closed)
                {
                    double ex = rev ? items[idx].x1 : items[idx].x2;
                    double ey = rev ? items[idx].y1 : items[idx].y2;
                    double ez = rev ? items[idx].z1 : items[idx].z2;
                    dwg_polyline_add_vertex2(hpl, ex, ey, ez, 0.0, 0.0, 0.0);
                }

                erased[items[idx].orig_index] = 1;
            }

            dwg_polyline_set_closed(hpl, closed);
        }

        created[created_count] = new_pl;
        created_count++;
    }

    /* erase every entity that ended up in a >=2-entity chain -- done as
       its own pass, after all chain-building/polyline-vertex work is
       finished, so no cached items[].x/y/z/bulge value is ever read
       after its source entity is destroyed */
    for (i = 0UL; i < orig_count; i++)
    {
        if (erased[i])
            dwg_document_remove_entity(hDwg, orig[i]);
    }

    dwg_document_sel_clear(hDwg);

    for (i = 0UL; i < created_count; i++)
        dwg_document_sel_add(hDwg, created[i]);

    for (i = 0UL; i < orig_count; i++)
    {
        if (!erased[i])
            dwg_document_sel_add(hDwg, orig[i]);
    }

    free(orig);
    free(erased);
    free(items);
    free(chain_idx);
    free(back_idx);
    free(chain_rev);
    free(back_rev);
    free(created);

    return created_count;
}

unsigned long dwg_sel_copy(HDWG hDwg)
{
    unsigned long i, count, made = 0UL;
    HENTITY *originals;

    if (hDwg == NULL)
        return 0UL;

    count = dwg_document_sel_count(hDwg);
    if (count == 0UL)
        return 0UL;

    /* Snapshot the originals before mutating the selection, since
       dwg_document_sel_add below (via the new copies) would otherwise
       grow/reallocate the very array dwg_document_sel_get(hDwg, i) is
       reading from mid-loop. */
    originals = (HENTITY *)malloc((size_t)count * sizeof(HENTITY));
    if (originals == NULL)
        return 0UL;

    for (i = 0UL; i < count; i++)
        originals[i] = dwg_document_sel_get(hDwg, i);

    dwg_document_sel_clear(hDwg);

    for (i = 0UL; i < count; i++)
    {
        HENTITY copy = dwg_entity_copy(hDwg, originals[i]);
        if (copy != NULL)
        {
            dwg_document_sel_add(hDwg, copy);
            made++;
        }
    }

    free(originals);

    return made;
}
