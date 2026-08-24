#include <math.h>

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
#include "dwg_block.h"
#include "dwg_document.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void move_point(DWG_POINT3D *p, double dx, double dy, double dz)
{
    p->x += dx;
    p->y += dy;
    p->z += dz;
}

static void rotate_point(DWG_POINT3D *p, double cx, double cy, double rad)
{
    double dx = p->x - cx;
    double dy = p->y - cy;
    double c = cos(rad);
    double s = sin(rad);

    p->x = cx + dx * c - dy * s;
    p->y = cy + dx * s + dy * c;
}

static void scale_point_xyz(DWG_POINT3D *p, double cx, double cy, double cz,
                            double sx, double sy, double sz)
{
    p->x = cx + (p->x - cx) * sx;
    p->y = cy + (p->y - cy) * sy;
    p->z = cz + (p->z - cz) * sz;
}

static void mirror_point(DWG_POINT3D *p, double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    double vx, vy, proj_len, perp_x, perp_y;

    if (len2 == 0.0)
        return; /* degenerate mirror line: leave point unchanged */

    vx = p->x - x1;
    vy = p->y - y1;
    proj_len = (vx * dx + vy * dy) / len2;
    perp_x = vx - proj_len * dx;
    perp_y = vy - proj_len * dy;

    p->x -= 2.0 * perp_x;
    p->y -= 2.0 * perp_y;
}

/* Applies fn to every point relevant to this entity's geometry. The
   four transform entry points below each pass a small closure-like set
   of parameters through 'ctx' since C89 has no closures; simplest is to
   just duplicate the per-type switch four times (move/rotate/scale/
   mirror) rather than build a generic point-visitor -- keeps each
   transform's math localized and easy to verify against the formulas
   above instead of threading a function pointer through every case. */

void dwg_entity_move(HENTITY hEntity, double dx, double dy, double dz)
{
    if (hEntity == NULL || hEntity->geometry == NULL)
        return;

    switch (hEntity->type)
    {
    case DWG_ENTITY_POINT:
        move_point((DWG_POINT3D *)hEntity->geometry, dx, dy, dz);
        break;

    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)hEntity->geometry;
        move_point(&l->start, dx, dy, dz);
        move_point(&l->end, dx, dy, dz);
        break;
    }
    case DWG_ENTITY_CIRCLE:
        move_point(&((DWG_CIRCLE3D *)hEntity->geometry)->center, dx, dy, dz);
        break;

    case DWG_ENTITY_ARC:
        move_point(&((DWG_ARC3D *)hEntity->geometry)->center, dx, dy, dz);
        break;

    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        dwg_text_get_point(hEntity, &x, &y, &z);
        dwg_text_get_point0(hEntity, &x0, &y0, &z0);
        dwg_text_set_point(hEntity, x + dx, y + dy, z + dz);
        dwg_text_set_point0(hEntity, x0 + dx, y0 + dy, z0 + dz);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        dwg_mtext_get_point(hEntity, &x, &y, &z);
        dwg_mtext_set_point(hEntity, x + dx, y + dy, z + dz);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z;
        dwg_insert_get_point(hEntity, &x, &y, &z);
        dwg_insert_set_point(hEntity, x + dx, y + dy, z + dz);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(hEntity);
        HVERTEX v = dwg_polyline_first_vertex(pl);
        while (v != NULL)
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            dwg_vertex_set_point(v, x + dx, y + dy, z + dz);
            v = dwg_polyline_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v = dwg_hatch_first_boundary_point(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            dwg_vertex_set_point(v, x + dx, y + dy, z + dz);
            v = dwg_hatch_next_boundary_point(v);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v = dwg_leader_first_vertex(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            dwg_vertex_get_point(v, &x, &y, &z);
            dwg_vertex_set_point(v, x + dx, y + dy, z + dz);
            v = dwg_leader_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        /* DWG_SOLID3D and DWG_FACE3D share the same p1..p4 layout */
        DWG_SOLID3D *s = (DWG_SOLID3D *)hEntity->geometry;
        move_point(&s->p1, dx, dy, dz);
        move_point(&s->p2, dx, dy, dz);
        move_point(&s->p3, dx, dy, dz);
        move_point(&s->p4, dx, dy, dz);
        break;
    }
    default:
        break;
    }
}

void dwg_entity_rotate(HENTITY hEntity,
                       double cx, double cy, double cz,
                       double angle_degrees)
{
    double rad;

    if (hEntity == NULL || hEntity->geometry == NULL)
        return;

    rad = angle_degrees * M_PI / 180.0;
    (void)cz; /* rotation is about the Z axis (2D CAD convention): only X,Y turn */

    switch (hEntity->type)
    {
    case DWG_ENTITY_POINT:
        rotate_point((DWG_POINT3D *)hEntity->geometry, cx, cy, rad);
        break;

    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)hEntity->geometry;
        rotate_point(&l->start, cx, cy, rad);
        rotate_point(&l->end, cx, cy, rad);
        break;
    }
    case DWG_ENTITY_CIRCLE:
        rotate_point(&((DWG_CIRCLE3D *)hEntity->geometry)->center, cx, cy, rad);
        break;

    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)hEntity->geometry;
        rotate_point(&a->center, cx, cy, rad);
        a->start_angle += angle_degrees;
        a->end_angle += angle_degrees;
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        DWG_POINT3D p, p0;

        dwg_text_get_point(hEntity, &x, &y, &z);
        dwg_text_get_point0(hEntity, &x0, &y0, &z0);
        p.x = x; p.y = y; p.z = z;
        p0.x = x0; p0.y = y0; p0.z = z0;
        rotate_point(&p, cx, cy, rad);
        rotate_point(&p0, cx, cy, rad);
        dwg_text_set_point(hEntity, p.x, p.y, p.z);
        dwg_text_set_point0(hEntity, p0.x, p0.y, p0.z);
        dwg_text_set_angle(hEntity, dwg_text_get_angle(hEntity) + angle_degrees);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_mtext_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        rotate_point(&p, cx, cy, rad);
        dwg_mtext_set_point(hEntity, p.x, p.y, p.z);
        dwg_mtext_set_angle(hEntity, dwg_mtext_get_angle(hEntity) + angle_degrees);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_insert_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        rotate_point(&p, cx, cy, rad);
        dwg_insert_set_point(hEntity, p.x, p.y, p.z);
        dwg_insert_set_angle(hEntity, dwg_insert_get_angle(hEntity) + angle_degrees);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(hEntity);
        HVERTEX v = dwg_polyline_first_vertex(pl);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            rotate_point(&p, cx, cy, rad);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_polyline_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v = dwg_hatch_first_boundary_point(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            rotate_point(&p, cx, cy, rad);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_hatch_next_boundary_point(v);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v = dwg_leader_first_vertex(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            rotate_point(&p, cx, cy, rad);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_leader_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)hEntity->geometry;
        rotate_point(&s->p1, cx, cy, rad);
        rotate_point(&s->p2, cx, cy, rad);
        rotate_point(&s->p3, cx, cy, rad);
        rotate_point(&s->p4, cx, cy, rad);
        break;
    }
    default:
        break;
    }
}

/* Shared by dwg_entity_scale (uniform, sx==sy==sz) and dwg_entity_scale_xyz
   (independent per-axis factors -- real vecad's own base primitive,
   vuScalePoint, supports this; see reverse/vu_math_notes.md). CIRCLE/ARC
   radius and TEXT/MTEXT height have no exact meaning under non-uniform
   scale (same reasoning as apply_insert_transform's non-uniform INSERT
   explode case above): both use the average of sx,sy, exact whenever
   sx==sy and a documented approximation otherwise. */
static void entity_scale_xyz(HENTITY hEntity,
                             double cx, double cy, double cz,
                             double sx, double sy, double sz)
{
    double avg_scale;

    if (hEntity == NULL || hEntity->geometry == NULL)
        return;

    avg_scale = (sx + sy) / 2.0;

    switch (hEntity->type)
    {
    case DWG_ENTITY_POINT:
        scale_point_xyz((DWG_POINT3D *)hEntity->geometry, cx, cy, cz, sx, sy, sz);
        break;

    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)hEntity->geometry;
        scale_point_xyz(&l->start, cx, cy, cz, sx, sy, sz);
        scale_point_xyz(&l->end, cx, cy, cz, sx, sy, sz);
        break;
    }
    case DWG_ENTITY_CIRCLE:
    {
        DWG_CIRCLE3D *c = (DWG_CIRCLE3D *)hEntity->geometry;
        scale_point_xyz(&c->center, cx, cy, cz, sx, sy, sz);
        c->radius *= avg_scale;
        break;
    }
    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)hEntity->geometry;
        scale_point_xyz(&a->center, cx, cy, cz, sx, sy, sz);
        a->radius *= avg_scale;
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        DWG_POINT3D p, p0;
        dwg_text_get_point(hEntity, &x, &y, &z);
        dwg_text_get_point0(hEntity, &x0, &y0, &z0);
        p.x = x; p.y = y; p.z = z;
        p0.x = x0; p0.y = y0; p0.z = z0;
        scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
        scale_point_xyz(&p0, cx, cy, cz, sx, sy, sz);
        dwg_text_set_point(hEntity, p.x, p.y, p.z);
        dwg_text_set_point0(hEntity, p0.x, p0.y, p0.z);
        dwg_text_set_height(hEntity, dwg_text_get_height(hEntity) * avg_scale);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_mtext_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
        dwg_mtext_set_point(hEntity, p.x, p.y, p.z);
        dwg_mtext_set_height(hEntity, dwg_mtext_get_height(hEntity) * avg_scale);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z, isx, isy, isz;
        DWG_POINT3D p;
        dwg_insert_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
        dwg_insert_set_point(hEntity, p.x, p.y, p.z);
        dwg_insert_get_scale(hEntity, &isx, &isy, &isz);
        dwg_insert_set_scale(hEntity, isx * sx, isy * sy, isz * sz);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(hEntity);
        HVERTEX v = dwg_polyline_first_vertex(pl);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_polyline_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v = dwg_hatch_first_boundary_point(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_hatch_next_boundary_point(v);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v = dwg_leader_first_vertex(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            scale_point_xyz(&p, cx, cy, cz, sx, sy, sz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_leader_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)hEntity->geometry;
        scale_point_xyz(&s->p1, cx, cy, cz, sx, sy, sz);
        scale_point_xyz(&s->p2, cx, cy, cz, sx, sy, sz);
        scale_point_xyz(&s->p3, cx, cy, cz, sx, sy, sz);
        scale_point_xyz(&s->p4, cx, cy, cz, sx, sy, sz);
        break;
    }
    default:
        break;
    }
}

void dwg_entity_scale(HENTITY hEntity,
                      double cx, double cy, double cz,
                      double factor)
{
    entity_scale_xyz(hEntity, cx, cy, cz, factor, factor, factor);
}

/* Independent per-axis scale factors, matching real vecad's base
   vuScalePoint primitive (see reverse/vu_math_notes.md) -- our engine's
   own dwg_entity_scale only exposed a uniform factor until this was
   added. See entity_scale_xyz's comment above for how CIRCLE/ARC radius
   and TEXT/MTEXT height are handled when sx != sy. */
void dwg_entity_scale_xyz(HENTITY hEntity,
                          double cx, double cy, double cz,
                          double sx, double sy, double sz)
{
    entity_scale_xyz(hEntity, cx, cy, cz, sx, sy, sz);
}

void dwg_entity_mirror(HENTITY hEntity,
                       double x1, double y1,
                       double x2, double y2)
{
    if (hEntity == NULL || hEntity->geometry == NULL)
        return;

    switch (hEntity->type)
    {
    case DWG_ENTITY_POINT:
        mirror_point((DWG_POINT3D *)hEntity->geometry, x1, y1, x2, y2);
        break;

    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)hEntity->geometry;
        mirror_point(&l->start, x1, y1, x2, y2);
        mirror_point(&l->end, x1, y1, x2, y2);
        break;
    }
    case DWG_ENTITY_CIRCLE:
        mirror_point(&((DWG_CIRCLE3D *)hEntity->geometry)->center, x1, y1, x2, y2);
        break;

    case DWG_ENTITY_ARC:
    {
        /* Mirroring swaps handedness: start/end angles swap and the
           mirror-line's own angle reflects them, same convention
           AutoCAD's MIRROR command uses for arcs. */
        DWG_ARC3D *a = (DWG_ARC3D *)hEntity->geometry;
        double mirror_axis_deg = atan2(y2 - y1, x2 - x1) * 180.0 / M_PI;
        double new_start = 2.0 * mirror_axis_deg - a->end_angle;
        double new_end = 2.0 * mirror_axis_deg - a->start_angle;

        mirror_point(&a->center, x1, y1, x2, y2);
        a->start_angle = new_start;
        a->end_angle = new_end;
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        DWG_POINT3D p, p0;
        dwg_text_get_point(hEntity, &x, &y, &z);
        dwg_text_get_point0(hEntity, &x0, &y0, &z0);
        p.x = x; p.y = y; p.z = z;
        p0.x = x0; p0.y = y0; p0.z = z0;
        mirror_point(&p, x1, y1, x2, y2);
        mirror_point(&p0, x1, y1, x2, y2);
        dwg_text_set_point(hEntity, p.x, p.y, p.z);
        dwg_text_set_point0(hEntity, p0.x, p0.y, p0.z);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_mtext_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        mirror_point(&p, x1, y1, x2, y2);
        dwg_mtext_set_point(hEntity, p.x, p.y, p.z);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_insert_get_point(hEntity, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        mirror_point(&p, x1, y1, x2, y2);
        dwg_insert_set_point(hEntity, p.x, p.y, p.z);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(hEntity);
        HVERTEX v = dwg_polyline_first_vertex(pl);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            mirror_point(&p, x1, y1, x2, y2);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_polyline_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v = dwg_hatch_first_boundary_point(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            mirror_point(&p, x1, y1, x2, y2);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_hatch_next_boundary_point(v);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v = dwg_leader_first_vertex(hEntity);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            mirror_point(&p, x1, y1, x2, y2);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_leader_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)hEntity->geometry;
        mirror_point(&s->p1, x1, y1, x2, y2);
        mirror_point(&s->p2, x1, y1, x2, y2);
        mirror_point(&s->p3, x1, y1, x2, y2);
        mirror_point(&s->p4, x1, y1, x2, y2);
        break;
    }
    default:
        break;
    }
}

static void copy_common_properties(HENTITY src, HENTITY dst)
{
    const void *ex_data;
    unsigned long ex_data_size;

    dwg_entity_put_layer(dst, dwg_entity_get_layer(src));
    dwg_entity_put_linetype(dst, dwg_entity_get_linetype(src));
    dwg_entity_put_color(dst, dwg_entity_get_color(src));

    ex_data = dwg_entity_get_ex_data(src, &ex_data_size);
    if (ex_data != NULL && ex_data_size > 0UL)
        dwg_entity_put_ex_data(dst, ex_data, ex_data_size);
}

HENTITY dwg_entity_copy(HDWG hDwg, HENTITY hEntity)
{
    HENTITY dst = NULL;

    if (hDwg == NULL || hEntity == NULL || hEntity->geometry == NULL)
        return NULL;

    switch (hEntity->type)
    {
    case DWG_ENTITY_POINT:
    {
        DWG_POINT3D *p = (DWG_POINT3D *)hEntity->geometry;
        dst = dwg_add_point(hDwg, p->x, p->y, p->z);
        break;
    }
    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)hEntity->geometry;
        dst = dwg_add_line(hDwg, l->start.x, l->start.y, l->start.z,
                           l->end.x, l->end.y, l->end.z);
        break;
    }
    case DWG_ENTITY_CIRCLE:
    {
        DWG_CIRCLE3D *c = (DWG_CIRCLE3D *)hEntity->geometry;
        dst = dwg_add_circle(hDwg, c->center.x, c->center.y, c->center.z, c->radius);
        break;
    }
    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)hEntity->geometry;
        dst = dwg_add_arc(hDwg, a->center.x, a->center.y, a->center.z,
                          a->radius, a->start_angle, a->end_angle);
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        dwg_text_get_point(hEntity, &x, &y, &z);
        dst = dwg_add_text(hDwg, x, y, z, dwg_text_get_height(hEntity),
                           dwg_text_get_angle(hEntity), dwg_text_get_text(hEntity));
        if (dst != NULL)
        {
            dwg_text_get_point0(hEntity, &x0, &y0, &z0);
            dwg_text_set_point0(dst, x0, y0, z0);
            dwg_text_set_width_factor(dst, dwg_text_get_width_factor(hEntity));
            dwg_text_set_oblique(dst, dwg_text_get_oblique(hEntity));
            dwg_text_set_align(dst, dwg_text_get_align(hEntity));
            dwg_text_set_backward(dst, dwg_text_get_backward(hEntity));
            dwg_text_set_upside_down(dst, dwg_text_get_upside_down(hEntity));
            dwg_text_set_style_name(dst, dwg_text_get_style_name(hEntity));
        }
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        dwg_mtext_get_point(hEntity, &x, &y, &z);
        dst = dwg_add_mtext(hDwg, x, y, z, dwg_mtext_get_height(hEntity),
                            dwg_mtext_get_rect_width(hEntity), dwg_mtext_get_text(hEntity));
        if (dst != NULL)
        {
            dwg_mtext_set_angle(dst, dwg_mtext_get_angle(hEntity));
            dwg_mtext_set_line_space(dst, dwg_mtext_get_line_space(hEntity));
            dwg_mtext_set_attach(dst, dwg_mtext_get_attach(hEntity));
            dwg_mtext_set_style_name(dst, dwg_mtext_get_style_name(hEntity));
        }
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z, sx, sy, sz;
        dwg_insert_get_point(hEntity, &x, &y, &z);
        dst = dwg_add_insert(hDwg, dwg_insert_get_block_name(hEntity), x, y, z,
                             dwg_insert_get_angle(hEntity));
        if (dst != NULL)
        {
            dwg_insert_get_scale(hEntity, &sx, &sy, &sz);
            dwg_insert_set_scale(dst, sx, sy, sz);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)hEntity->geometry;
        dst = dwg_add_solid(hDwg, s->p1.x, s->p1.y, s->p1.z, s->p2.x, s->p2.y, s->p2.z,
                            s->p3.x, s->p3.y, s->p3.z, s->p4.x, s->p4.y, s->p4.z);
        break;
    }
    case DWG_ENTITY_FACE:
    {
        DWG_FACE3D *f = (DWG_FACE3D *)hEntity->geometry;
        dst = dwg_add_face(hDwg, f->p1.x, f->p1.y, f->p1.z, f->p2.x, f->p2.y, f->p2.z,
                           f->p3.x, f->p3.y, f->p3.z, f->p4.x, f->p4.y, f->p4.z);
        if (dst != NULL)
            dwg_face_set_edge_flags(dst, f->edge_flags);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE src_pl = dwg_polyline_from_entity(hEntity);
        HPOLYLINE dst_pl;
        HVERTEX v;

        dst = dwg_add_polyline(hDwg);
        dst_pl = dwg_polyline_from_entity(dst);
        if (dst_pl != NULL && src_pl != NULL)
        {
            dwg_polyline_set_closed(dst_pl, dwg_polyline_is_closed(src_pl));
            dwg_polyline_set_elevation(dst_pl, dwg_polyline_get_elevation(src_pl));

            v = dwg_polyline_first_vertex(src_pl);
            while (v != NULL)
            {
                double x, y, z;
                dwg_vertex_get_point(v, &x, &y, &z);
                dwg_polyline_add_vertex2(dst_pl, x, y, z,
                                         dwg_vertex_get_bulge(v), 0.0, 0.0);
                v = dwg_polyline_next_vertex(v);
            }
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v;
        dst = dwg_add_hatch(hDwg, dwg_hatch_get_pattern(hEntity),
                            dwg_hatch_get_angle(hEntity), dwg_hatch_get_scale(hEntity),
                            dwg_hatch_get_solid(hEntity));
        if (dst != NULL)
        {
            v = dwg_hatch_first_boundary_point(hEntity);
            while (v != NULL)
            {
                double x, y, z;
                dwg_vertex_get_point(v, &x, &y, &z);
                dwg_hatch_add_boundary_point(dst, x, y, z);
                v = dwg_hatch_next_boundary_point(v);
            }
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v;
        dst = dwg_add_leader(hDwg);
        if (dst != NULL)
        {
            dwg_leader_set_arrow_size(dst, dwg_leader_get_arrow_size(hEntity));
            dwg_leader_set_spline(dst, dwg_leader_get_spline(hEntity));
            dwg_leader_set_text(dst, dwg_leader_get_text(hEntity));
            dwg_leader_set_text_height(dst, dwg_leader_get_text_height(hEntity));

            v = dwg_leader_first_vertex(hEntity);
            while (v != NULL)
            {
                double x, y, z;
                dwg_vertex_get_point(v, &x, &y, &z);
                dwg_leader_add_vertex(dst, x, y, z);
                v = dwg_leader_next_vertex(v);
            }
        }
        break;
    }
    default:
        return NULL;
    }

    if (dst != NULL)
        copy_common_properties(hEntity, dst);

    return dst;
}

/*
 * Bulge-to-arc conversion, standard CAD math: bulge = tan(included_angle/4),
 * positive bulge = CCW arc from the segment's first vertex to its second
 * (standard DXF/AutoCAD bulge convention).
 *
 * sagitta = (chord/2) * bulge is the signed perpendicular distance from the
 * chord's midpoint toward "left of the start->end direction" (rotate the
 * chord vector +90 degrees). The CENTER lies further along that same
 * perpendicular, at distance (radius - sagitta) from the chord midpoint --
 * NOT the arc's own bulge point, which sits on the opposite side (a minor
 * arc's center is always on the far side of the chord from where the arc
 * itself bulges out). Verified numerically against a diameter/semicircle
 * pair (P1=(1,0), P2=(-1,0), bulge~1 traces the CCW/upper semicircle
 * through (0,1), confirmed by direct angle parametrization) and against a
 * quarter-circle on the unit circle (P1=(1,0), P2=(0,1), bulge=tan(22.5deg)
 * reproduces center (0,0) exactly) before trusting this formula here.
 * The property the caller actually depends on -- that start_angle/
 * end_angle on the resulting center+radius reproduce P1/P2 exactly -- is
 * re-checked directly in tests/test_transform.c rather than trusted from
 * this derivation alone.
 */
static void bulge_to_arc(double x1, double y1, double x2, double y2, double bulge,
                         double *cx, double *cy, double *radius,
                         double *start_angle, double *end_angle)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double chord = sqrt(dx * dx + dy * dy);
    double included = 4.0 * atan(bulge);
    double sagitta = (chord / 2.0) * bulge;
    double perp_x = -dy / chord;
    double perp_y = dx / chord;
    double mid_x = (x1 + x2) / 2.0;
    double mid_y = (y1 + y2) / 2.0;

    *radius = chord / (2.0 * fabs(sin(included / 2.0)));
    *cx = mid_x + perp_x * (*radius - sagitta);
    *cy = mid_y + perp_y * (*radius - sagitta);
    *start_angle = atan2(y1 - *cy, x1 - *cx) * 180.0 / M_PI;
    *end_angle = atan2(y2 - *cy, x2 - *cx) * 180.0 / M_PI;
}

static unsigned long explode_polyline(HDWG hDwg, HENTITY hEntity)
{
    HPOLYLINE pl = dwg_polyline_from_entity(hEntity);
    HVERTEX v, vnext;
    unsigned long created = 0UL;
    long closed;

    if (pl == NULL)
        return 0UL;

    closed = dwg_polyline_is_closed(pl);

    for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
    {
        double x1, y1, z1, x2, y2, z2, bulge;
        HENTITY seg = NULL;

        vnext = dwg_polyline_next_vertex(v);
        if (vnext == NULL)
        {
            if (!closed)
                break;
            vnext = dwg_polyline_first_vertex(pl);
        }

        dwg_vertex_get_point(v, &x1, &y1, &z1);
        dwg_vertex_get_point(vnext, &x2, &y2, &z2);
        bulge = dwg_vertex_get_bulge(v);

        if (x1 == x2 && y1 == y2)
            continue; /* degenerate zero-length segment: nothing to draw */

        if (bulge == 0.0)
        {
            seg = dwg_add_line(hDwg, x1, y1, z1, x2, y2, z2);
        }
        else
        {
            double acx, acy, aradius, astart, aend;
            bulge_to_arc(x1, y1, x2, y2, bulge, &acx, &acy, &aradius, &astart, &aend);
            seg = dwg_add_arc(hDwg, acx, acy, z1, aradius, astart, aend);
        }

        if (seg != NULL)
        {
            copy_common_properties(hEntity, seg);
            created++;
        }
    }

    return created;
}

/* Scales by (sx,sy,sz) about the origin, rotates by rad about the Z axis,
   then translates by (ix,iy,iz) -- the standard block-reference transform
   pipeline, applied directly to points rather than by composing
   dwg_entity_scale/rotate/move so that non-uniform sx != sy is handled
   exactly for point-based geometry (everything except CIRCLE/ARC, whose
   radius has no exact meaning under non-uniform scale since this engine
   has no ellipse-from-arc conversion -- those two approximate with the
   average of sx,sy, exact whenever a block is inserted at uniform scale,
   which is the overwhelmingly common real-world case). */
static void insert_transform_point(DWG_POINT3D *p, double sx, double sy, double sz,
                                   double rad, double ix, double iy, double iz)
{
    double x = p->x * sx;
    double y = p->y * sy;
    double z = p->z * sz;
    double c = cos(rad);
    double s = sin(rad);

    p->x = ix + x * c - y * s;
    p->y = iy + x * s + y * c;
    p->z = iz + z;
}

static void apply_insert_transform(HENTITY e, double sx, double sy, double sz,
                                   double angle_degrees, double ix, double iy, double iz)
{
    double rad = angle_degrees * M_PI / 180.0;
    double avg_scale = (sx + sy) / 2.0;

    if (e == NULL || e->geometry == NULL)
        return;

    switch (e->type)
    {
    case DWG_ENTITY_POINT:
        insert_transform_point((DWG_POINT3D *)e->geometry, sx, sy, sz, rad, ix, iy, iz);
        break;

    case DWG_ENTITY_LINE:
    {
        DWG_LINE3D *l = (DWG_LINE3D *)e->geometry;
        insert_transform_point(&l->start, sx, sy, sz, rad, ix, iy, iz);
        insert_transform_point(&l->end, sx, sy, sz, rad, ix, iy, iz);
        break;
    }
    case DWG_ENTITY_CIRCLE:
    {
        DWG_CIRCLE3D *c = (DWG_CIRCLE3D *)e->geometry;
        insert_transform_point(&c->center, sx, sy, sz, rad, ix, iy, iz);
        c->radius *= avg_scale;
        break;
    }
    case DWG_ENTITY_ARC:
    {
        DWG_ARC3D *a = (DWG_ARC3D *)e->geometry;
        insert_transform_point(&a->center, sx, sy, sz, rad, ix, iy, iz);
        a->radius *= avg_scale;
        a->start_angle += angle_degrees;
        a->end_angle += angle_degrees;
        break;
    }
    case DWG_ENTITY_TEXT:
    {
        double x, y, z, x0, y0, z0;
        DWG_POINT3D p, p0;
        dwg_text_get_point(e, &x, &y, &z);
        dwg_text_get_point0(e, &x0, &y0, &z0);
        p.x = x; p.y = y; p.z = z;
        p0.x = x0; p0.y = y0; p0.z = z0;
        insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
        insert_transform_point(&p0, sx, sy, sz, rad, ix, iy, iz);
        dwg_text_set_point(e, p.x, p.y, p.z);
        dwg_text_set_point0(e, p0.x, p0.y, p0.z);
        dwg_text_set_height(e, dwg_text_get_height(e) * avg_scale);
        dwg_text_set_angle(e, dwg_text_get_angle(e) + angle_degrees);
        break;
    }
    case DWG_ENTITY_MTEXT:
    {
        double x, y, z;
        DWG_POINT3D p;
        dwg_mtext_get_point(e, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
        dwg_mtext_set_point(e, p.x, p.y, p.z);
        dwg_mtext_set_height(e, dwg_mtext_get_height(e) * avg_scale);
        dwg_mtext_set_angle(e, dwg_mtext_get_angle(e) + angle_degrees);
        break;
    }
    case DWG_ENTITY_INSERT:
    {
        double x, y, z, isx, isy, isz;
        DWG_POINT3D p;
        dwg_insert_get_point(e, &x, &y, &z);
        p.x = x; p.y = y; p.z = z;
        insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
        dwg_insert_set_point(e, p.x, p.y, p.z);
        dwg_insert_get_scale(e, &isx, &isy, &isz);
        dwg_insert_set_scale(e, isx * sx, isy * sy, isz * sz);
        dwg_insert_set_angle(e, dwg_insert_get_angle(e) + angle_degrees);
        break;
    }
    case DWG_ENTITY_POLYLINE:
    {
        HPOLYLINE pl = dwg_polyline_from_entity(e);
        HVERTEX v = dwg_polyline_first_vertex(pl);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_polyline_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_HATCH:
    {
        HVERTEX v = dwg_hatch_first_boundary_point(e);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_hatch_next_boundary_point(v);
        }
        break;
    }
    case DWG_ENTITY_LEADER:
    {
        HVERTEX v = dwg_leader_first_vertex(e);
        while (v != NULL)
        {
            double x, y, z;
            DWG_POINT3D p;
            dwg_vertex_get_point(v, &x, &y, &z);
            p.x = x; p.y = y; p.z = z;
            insert_transform_point(&p, sx, sy, sz, rad, ix, iy, iz);
            dwg_vertex_set_point(v, p.x, p.y, p.z);
            v = dwg_leader_next_vertex(v);
        }
        break;
    }
    case DWG_ENTITY_SOLID:
    case DWG_ENTITY_FACE:
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)e->geometry;
        insert_transform_point(&s->p1, sx, sy, sz, rad, ix, iy, iz);
        insert_transform_point(&s->p2, sx, sy, sz, rad, ix, iy, iz);
        insert_transform_point(&s->p3, sx, sy, sz, rad, ix, iy, iz);
        insert_transform_point(&s->p4, sx, sy, sz, rad, ix, iy, iz);
        break;
    }
    default:
        break;
    }
}

static unsigned long explode_insert(HDWG hDwg, HENTITY hEntity)
{
    HBLOCK block = dwg_document_get_block(hDwg, dwg_insert_get_block_name(hEntity));
    HENTITY src;
    unsigned long created = 0UL;
    double ix, iy, iz, sx, sy, sz, angle;

    if (block == NULL)
        return 0UL;

    dwg_insert_get_point(hEntity, &ix, &iy, &iz);
    dwg_insert_get_scale(hEntity, &sx, &sy, &sz);
    angle = dwg_insert_get_angle(hEntity);

    for (src = dwg_block_first_entity(block); src != NULL; src = dwg_block_next_entity(src))
    {
        HENTITY copy = dwg_entity_copy(hDwg, src);
        if (copy == NULL)
            continue;

        apply_insert_transform(copy, sx, sy, sz, angle, ix, iy, iz);
        created++;
    }

    return created;
}

unsigned long dwg_entity_explode(HDWG hDwg, HENTITY hEntity)
{
    if (hDwg == NULL || hEntity == NULL || hEntity->geometry == NULL)
        return 0UL;

    switch (hEntity->type)
    {
    case DWG_ENTITY_POLYLINE:
        return explode_polyline(hDwg, hEntity);

    case DWG_ENTITY_INSERT:
        return explode_insert(hDwg, hEntity);

    default:
        return 0UL;
    }
}

#define DWG_EDIT_MAX_HITS 2

static double norm360(double deg)
{
    while (deg < 0.0)
        deg += 360.0;
    while (deg >= 360.0)
        deg -= 360.0;
    return deg;
}

/* Same CCW-sweep-from-start_angle-to-end_angle convention verified for
   bulge_to_arc above: the sweep is the CCW distance from start to end,
   wrapping through 360 if end < start (raw, unnormalized angles, same
   as dwg_entity_rotate leaves them). */
static int angle_in_arc_sweep(double test_deg, double start_deg, double end_deg)
{
    double sweep = norm360(end_deg - start_deg);
    double rel = norm360(test_deg - start_deg);

    if (sweep < 1e-9)
        sweep = 360.0;

    return rel <= sweep + 1e-9;
}

/* Intersects the INFINITE line through (x1,y1)-(x2,y2) -- hEntity's own
   line, extended in both directions -- against hBoundary's actual
   (bounded) geometry: a LINE boundary requires the hit to fall within
   the boundary's own u in [0,1], an ARC boundary requires the hit's
   angle to fall within its start_angle/end_angle sweep, a CIRCLE
   boundary has no such restriction (the whole circle counts). Writes
   up to DWG_EDIT_MAX_HITS t-values (parametrized along hEntity: t=0 at
   (x1,y1), t=1 at (x2,y2)) into out_t and returns how many were found.
   Trim/extend then filter these by t range (inside vs outside the
   segment) themselves. */
static int line_boundary_intersections(double x1, double y1, double x2, double y2,
                                       HENTITY boundary, double *out_t)
{
    double d1x = x2 - x1;
    double d1y = y2 - y1;
    int count = 0;

    if (boundary == NULL || boundary->geometry == NULL)
        return 0;

    if (boundary->type == DWG_ENTITY_LINE)
    {
        DWG_LINE3D *b = (DWG_LINE3D *)boundary->geometry;
        double d2x = b->end.x - b->start.x;
        double d2y = b->end.y - b->start.y;
        double denom = d1x * d2y - d1y * d2x;

        if (fabs(denom) > 1e-12)
        {
            double t = ((b->start.x - x1) * d2y - (b->start.y - y1) * d2x) / denom;
            double u = ((b->start.x - x1) * d1y - (b->start.y - y1) * d1x) / denom;

            if (u >= -1e-9 && u <= 1.0 + 1e-9)
            {
                out_t[count] = t;
                count++;
            }
        }
    }
    else if (boundary->type == DWG_ENTITY_CIRCLE || boundary->type == DWG_ENTITY_ARC)
    {
        int is_arc = (boundary->type == DWG_ENTITY_ARC);
        double cx, cy, radius, start_angle = 0.0, end_angle = 0.0;
        double fx, fy, a, b, c, disc;

        if (is_arc)
        {
            DWG_ARC3D *arc = (DWG_ARC3D *)boundary->geometry;
            cx = arc->center.x; cy = arc->center.y; radius = arc->radius;
            start_angle = arc->start_angle; end_angle = arc->end_angle;
        }
        else
        {
            DWG_CIRCLE3D *circ = (DWG_CIRCLE3D *)boundary->geometry;
            cx = circ->center.x; cy = circ->center.y; radius = circ->radius;
        }

        fx = x1 - cx;
        fy = y1 - cy;
        a = d1x * d1x + d1y * d1y;
        b = 2.0 * (fx * d1x + fy * d1y);
        c = fx * fx + fy * fy - radius * radius;
        disc = b * b - 4.0 * a * c;

        if (a > 1e-12 && disc >= 0.0)
        {
            double sq = sqrt(disc);
            double ts[2];
            int n = (disc < 1e-12) ? 1 : 2;
            int i;

            ts[0] = (-b - sq) / (2.0 * a);
            ts[1] = (-b + sq) / (2.0 * a);

            for (i = 0; i < n && count < DWG_EDIT_MAX_HITS; i++)
            {
                if (is_arc)
                {
                    double px = x1 + ts[i] * d1x;
                    double py = y1 + ts[i] * d1y;
                    double ang = atan2(py - cy, px - cx) * 180.0 / M_PI;
                    if (!angle_in_arc_sweep(ang, start_angle, end_angle))
                        continue;
                }
                out_t[count] = ts[i];
                count++;
            }
        }
    }

    return count;
}

long dwg_entity_trim(HENTITY hEntity, HENTITY hBoundary, double px, double py)
{
    DWG_LINE3D *l;
    double t_hits[DWG_EDIT_MAX_HITS];
    int n, i;
    double d1x, d1y, len2, t_pick, best_t = 0.0;
    int have_best = 0;

    if (hEntity == NULL || hEntity->geometry == NULL || hEntity->type != DWG_ENTITY_LINE)
        return 0L;
    if (hBoundary == NULL || hBoundary->geometry == NULL)
        return 0L;

    l = (DWG_LINE3D *)hEntity->geometry;
    d1x = l->end.x - l->start.x;
    d1y = l->end.y - l->start.y;
    len2 = d1x * d1x + d1y * d1y;
    if (len2 < 1e-12)
        return 0L;

    t_pick = ((px - l->start.x) * d1x + (py - l->start.y) * d1y) / len2;

    n = line_boundary_intersections(l->start.x, l->start.y, l->end.x, l->end.y, hBoundary, t_hits);

    for (i = 0; i < n; i++)
    {
        double t = t_hits[i];

        if (t <= 1e-9 || t >= 1.0 - 1e-9)
            continue; /* must be strictly inside the current segment to trim */

        if (!have_best || fabs(t - t_pick) < fabs(best_t - t_pick))
        {
            best_t = t;
            have_best = 1;
        }
    }

    if (!have_best)
        return 0L;

    {
        double ix = l->start.x + best_t * d1x;
        double iy = l->start.y + best_t * d1y;
        double iz = l->start.z + best_t * (l->end.z - l->start.z);

        if (t_pick < best_t)
        {
            l->start.x = ix; l->start.y = iy; l->start.z = iz;
        }
        else
        {
            l->end.x = ix; l->end.y = iy; l->end.z = iz;
        }
    }

    return 1L;
}

long dwg_entity_extend(HENTITY hEntity, HENTITY hBoundary, double px, double py)
{
    DWG_LINE3D *l;
    double t_hits[DWG_EDIT_MAX_HITS];
    int n, i;
    double d1x, d1y, len2, t_pick, best_t = 0.0;
    int have_best = 0;
    int extend_start;

    if (hEntity == NULL || hEntity->geometry == NULL || hEntity->type != DWG_ENTITY_LINE)
        return 0L;
    if (hBoundary == NULL || hBoundary->geometry == NULL)
        return 0L;

    l = (DWG_LINE3D *)hEntity->geometry;
    d1x = l->end.x - l->start.x;
    d1y = l->end.y - l->start.y;
    len2 = d1x * d1x + d1y * d1y;
    if (len2 < 1e-12)
        return 0L;

    t_pick = ((px - l->start.x) * d1x + (py - l->start.y) * d1y) / len2;
    extend_start = (t_pick < 0.5) ? 1 : 0;

    n = line_boundary_intersections(l->start.x, l->start.y, l->end.x, l->end.y, hBoundary, t_hits);

    for (i = 0; i < n; i++)
    {
        double t = t_hits[i];

        if (extend_start)
        {
            if (t >= -1e-9)
                continue; /* not actually beyond the start */

            if (!have_best || t > best_t) /* closest to the segment: largest (least negative) t */
            {
                best_t = t;
                have_best = 1;
            }
        }
        else
        {
            if (t <= 1.0 + 1e-9)
                continue; /* not actually beyond the end */

            if (!have_best || t < best_t) /* closest to the segment: smallest t > 1 */
            {
                best_t = t;
                have_best = 1;
            }
        }
    }

    if (!have_best)
        return 0L;

    {
        double ix = l->start.x + best_t * d1x;
        double iy = l->start.y + best_t * d1y;
        double iz = l->start.z + best_t * (l->end.z - l->start.z);

        if (extend_start)
        {
            l->start.x = ix; l->start.y = iy; l->start.z = iz;
        }
        else
        {
            l->end.x = ix; l->end.y = iy; l->end.z = iz;
        }
    }

    return 1L;
}
