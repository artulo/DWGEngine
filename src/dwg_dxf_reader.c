#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dwg_file_io.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_layer.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_text.h"
#include "dwg_block.h"
#include "dwg_insert.h"
#include "dwg_linetype.h"
#include "dwg_style.h"
#include "dwg_mtext.h"
#include "dwg_hatch.h"
#include "dwg_solid.h"
#include "dwg_leader.h"
#include "dwg_transform.h"

#define DXF_LINE_MAX 512

typedef struct
{
    FILE *fp;

    int has_pending;
    int pending_code;
    char pending_value[DXF_LINE_MAX];
} DXF_READER;

static void dxf_trim_eol(char *s)
{
    size_t len;

    len = strlen(s);

    while (len > 0UL &&
           (s[len - 1UL] == '\n' || s[len - 1UL] == '\r' || s[len - 1UL] == ' '))
    {
        s[len - 1UL] = '\0';
        --len;
    }
}

static int dxf_read_raw_pair(FILE *fp, int *code, char *value)
{
    char line[DXF_LINE_MAX];

    if (fgets(line, sizeof(line), fp) == NULL)
        return 0;

    dxf_trim_eol(line);
    *code = atoi(line);

    if (fgets(line, sizeof(line), fp) == NULL)
        return 0;

    dxf_trim_eol(line);

    strncpy(value, line, DXF_LINE_MAX - 1UL);
    value[DXF_LINE_MAX - 1UL] = '\0';

    return 1;
}

static int dxf_next(DXF_READER *reader, int *code, char *value)
{
    if (reader->has_pending)
    {
        *code = reader->pending_code;
        strncpy(value, reader->pending_value, DXF_LINE_MAX - 1UL);
        value[DXF_LINE_MAX - 1UL] = '\0';
        reader->has_pending = 0;
        return 1;
    }

    return dxf_read_raw_pair(reader->fp, code, value);
}

static void dxf_pushback(DXF_READER *reader, int code, const char *value)
{
    reader->pending_code = code;
    strncpy(reader->pending_value, value, DXF_LINE_MAX - 1UL);
    reader->pending_value[DXF_LINE_MAX - 1UL] = '\0';
    reader->has_pending = 1;
}

/*
 * Aplica un codigo de grupo comun a cualquier entidad (capa, color,
 * tipo de linea). Devuelve 1 si el codigo fue reconocido y consumido.
 */
static int dxf_apply_common_code(HENTITY entity, int code, const char *value)
{
    switch (code)
    {
    case 8:
        dwg_entity_put_layer(entity, value);
        return 1;

    case 62:
        dwg_entity_put_color(entity, (unsigned short)atoi(value));
        return 1;

    case 6:
        dwg_entity_put_linetype(entity, value);
        return 1;

    default:
        return 0;
    }
}

static void dxf_read_point(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0;

    entity = dwg_add_point(hDwg, 0.0, 0.0, 0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_POINT3D *point = (DWG_POINT3D *)entity->geometry;
        point->x = x;
        point->y = y;
        point->z = z;
    }
}

static void dxf_read_line(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x1 = 0.0, y1 = 0.0, z1 = 0.0;
    double x2 = 0.0, y2 = 0.0, z2 = 0.0;

    entity = dwg_add_line(hDwg, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x1 = atof(value); break;
        case 20: y1 = atof(value); break;
        case 30: z1 = atof(value); break;
        case 11: x2 = atof(value); break;
        case 21: y2 = atof(value); break;
        case 31: z2 = atof(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_LINE3D *line = (DWG_LINE3D *)entity->geometry;
        line->start.x = x1; line->start.y = y1; line->start.z = z1;
        line->end.x   = x2; line->end.y   = y2; line->end.z   = z2;
    }
}

static void dxf_read_circle(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0, radius = 0.0;

    entity = dwg_add_circle(hDwg, 0.0, 0.0, 0.0, 0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 40: radius = atof(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_CIRCLE3D *circle = (DWG_CIRCLE3D *)entity->geometry;
        circle->center.x = x; circle->center.y = y; circle->center.z = z;
        circle->radius = radius;
    }
}

static void dxf_read_arc(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0, radius = 0.0;
    double start_angle = 0.0, end_angle = 0.0;

    entity = dwg_add_arc(hDwg, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 40: radius = atof(value); break;
        case 50: start_angle = atof(value); break;
        case 51: end_angle = atof(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_ARC3D *arc = (DWG_ARC3D *)entity->geometry;
        arc->center.x = x; arc->center.y = y; arc->center.z = z;
        arc->radius = radius;
        arc->start_angle = start_angle;
        arc->end_angle = end_angle;
    }
}

static void dxf_read_text(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0;
    double x0 = 0.0, y0 = 0.0, z0 = 0.0;
    double height = 0.0, angle = 0.0, width_factor = 1.0, oblique = 0.0;
    char text[DWG_TEXT_MAX];
    char style_name[DXF_LINE_MAX];
    long flags = 0L;
    int align = 0;
    int has_point0 = 0;

    style_name[0] = '\0';

    text[0] = '\0';

    entity = dwg_add_text(hDwg, 0.0, 0.0, 0.0, 0.0, 0.0, "");

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 11: x0 = atof(value); has_point0 = 1; break;
        case 21: y0 = atof(value); has_point0 = 1; break;
        case 31: z0 = atof(value); has_point0 = 1; break;
        case 40: height = atof(value); break;
        case 50: angle = atof(value); break;
        case 41: width_factor = atof(value); break;
        case 51: oblique = atof(value); break;
        case 71: flags = atol(value); break;
        case 72: align = atoi(value); break;
        case 1:
            strncpy(text, value, DWG_TEXT_MAX - 1);
            text[DWG_TEXT_MAX - 1] = '\0';
            break;
        case 7:
            strncpy(style_name, value, DXF_LINE_MAX - 1UL);
            style_name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        default: break;
        }
    }

    dwg_text_set_style_name(entity, style_name);
    dwg_text_set_point(entity, x, y, z);
    if (has_point0)
        dwg_text_set_point0(entity, x0, y0, z0);
    else
        dwg_text_set_point0(entity, x, y, z);
    dwg_text_set_text(entity, text);
    dwg_text_set_height(entity, height);
    dwg_text_set_angle(entity, angle);
    dwg_text_set_width_factor(entity, width_factor);
    dwg_text_set_oblique(entity, oblique);
    dwg_text_set_align(entity, (unsigned short)align);
    dwg_text_set_backward(entity, (flags & 2L) != 0L ? DWG_TRUE : DWG_FALSE);
    dwg_text_set_upside_down(entity, (flags & 4L) != 0L ? DWG_TRUE : DWG_FALSE);
}

static void dxf_read_vertex(HPOLYLINE polyline, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0, bulge = 0.0;

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 42: bulge = atof(value); break;
        default: break;
        }
    }

    dwg_polyline_add_vertex2(polyline, x, y, z, bulge, 0.0, 0.0);
}

static void dxf_read_polyline(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    HPOLYLINE polyline;
    int code;
    char value[DXF_LINE_MAX];
    long flags = 0L;

    entity = dwg_add_polyline(hDwg);
    polyline = dwg_polyline_from_entity(entity);

    /* Cabecera POLYLINE: codigos comunes + 70 (flags) + 30 (elevation) */
    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 70: flags = atol(value); break;
        case 30: dwg_polyline_set_elevation(polyline, atof(value)); break;
        default: break;
        }
    }

    dwg_polyline_set_closed(polyline, flags & 1L);

    /* Secuencia de VERTEX hasta SEQEND */
    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code != 0)
            continue; /* no deberia ocurrir aqui */

        if (strcmp(value, "VERTEX") == 0)
        {
            dxf_read_vertex(polyline, reader);
        }
        else if (strcmp(value, "SEQEND") == 0)
        {
            /* consumir posibles codigos de SEQEND hasta el proximo 0 */
            for (;;)
            {
                if (!dxf_next(reader, &code, value))
                    return;

                if (code == 0)
                {
                    dxf_pushback(reader, code, value);
                    return;
                }
            }
        }
        else
        {
            /* entidad inesperada dentro de POLYLINE: devolver y salir */
            dxf_pushback(reader, code, value);
            return;
        }
    }
}

static void dxf_skip_unknown_entity(DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            return;
        }
    }
}

static void dxf_read_mtext(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0;
    double height = 0.0, rect_width = 0.0, angle = 0.0, line_space = 1.0;
    int attach = DWG_MTEXT_ATTACH_TOP_LEFT;
    char text[DWG_MTEXT_MAX];
    char style_name[DXF_LINE_MAX];

    text[0] = '\0';
    style_name[0] = '\0';

    entity = dwg_add_mtext(hDwg, 0.0, 0.0, 0.0, 0.0, 0.0, "");

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 40: height = atof(value); break;
        case 41: rect_width = atof(value); break;
        case 44: line_space = atof(value); break;
        case 50: angle = atof(value); break;
        case 71: attach = atoi(value); break;
        case 7:
            strncpy(style_name, value, DXF_LINE_MAX - 1UL);
            style_name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 1:
        case 3:
            /* group 3 = continuation chunk in real DXF; our own writer
               always fits everything in one group-1 record, so just
               append whatever shows up */
            strncat(text, value, DWG_MTEXT_MAX - strlen(text) - 1UL);
            break;
        default: break;
        }
    }

    dwg_mtext_set_point(entity, x, y, z);
    dwg_mtext_set_height(entity, height);
    dwg_mtext_set_rect_width(entity, rect_width);
    dwg_mtext_set_line_space(entity, line_space);
    dwg_mtext_set_angle(entity, angle);
    dwg_mtext_set_attach(entity, (unsigned short)attach);
    dwg_mtext_set_style_name(entity, style_name);
    dwg_mtext_set_text(entity, text);
}

/*
 * LWPOLYLINE (the modern, single-entity "lightweight" polyline -- DXF's
 * usual form since AutoCAD 2000, distinct from the older multi-object
 * POLYLINE+VERTEX+SEQEND chain dxf_read_polyline above already handles)
 * was never parsed at all before this -- a real, sizable gap found via
 * a real file: ROTATORIO.dxf's page border/margin frame (a rectangle,
 * layer "margen 035", exactly A3-sized 297x420) is an LWPOLYLINE, and
 * was silently invisible (dxf_skip_unknown_entity) even after the
 * HATCH/color fixes, which is what Arturo was pointing at ("esta
 * disperso" / the missing border+axis markers in his reference image
 * comparison).
 *
 * Group order per vertex is 10/20 (point) then optional 40/41 (start/
 * end width) and 42 (bulge), all BEFORE the next vertex's own 10 --
 * buffered as a "pending vertex", flushed into the polyline the moment
 * a NEW 10 arrives (or at entity end). Elevation (a single constant Z
 * for the whole polyline, group 38) is applied the same way
 * dxf_read_polyline already applies POLYLINE's own group 30.
 */
static void dxf_read_lwpolyline(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    HPOLYLINE polyline;
    int code;
    char value[DXF_LINE_MAX];
    long flags = 0L;
    double px = 0.0, py = 0.0, p_bulge = 0.0, p_sw = 0.0, p_ew = 0.0;
    int have_pending = 0;

    entity = dwg_add_polyline(hDwg);
    polyline = dwg_polyline_from_entity(entity);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            break;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 70: flags = atol(value); break;
        case 38: dwg_polyline_set_elevation(polyline, atof(value)); break;
        case 10:
            if (have_pending)
                dwg_polyline_add_vertex2(polyline, px, py, 0.0, p_bulge, p_sw, p_ew);
            px = atof(value);
            py = 0.0; p_bulge = 0.0; p_sw = 0.0; p_ew = 0.0;
            have_pending = 1;
            break;
        case 20: py = atof(value); break;
        case 40: p_sw = atof(value); break;
        case 41: p_ew = atof(value); break;
        case 42: p_bulge = atof(value); break;
        default: break; /* 90 (vertex count), 43 (constant width): implied by the vertices themselves */
        }
    }

    if (have_pending)
        dwg_polyline_add_vertex2(polyline, px, py, 0.0, p_bulge, p_sw, p_ew);

    dwg_polyline_set_closed(polyline, flags & 1L);
}

/*
 * DIMENSION (the "cotas" Arturo flagged as missing). A full from-scratch
 * implementation would need real dimension geometry math (extension
 * lines, dimension line, arrowhead placement, text position) for each
 * of DIMENSION's several sub-types (linear/aligned/angular/radial/
 * diameter/ordinate) -- a big, error-prone undertaking. But a real
 * DXF file doesn't need any of that from a READER: AutoCAD itself
 * already computed and baked the dimension's full visual representation
 * (extension lines, dimension line, arrowhead SOLIDs, the MTEXT value)
 * into an anonymous block at SAVE time, and the DIMENSION entity's own
 * group 2 just names it (e.g. "*D19") -- confirmed against a real file:
 * ROTATORIO.dxf's *D19 block contains exactly a LINE, an MTEXT "R1675",
 * another LINE, and a SOLID arrowhead, all already in real WORLD
 * coordinates matching the rest of the drawing (not relative to a
 * local block origin needing an insertion transform). So rendering
 * DIMENSION correctly is just: find its block and copy its entities
 * in. Rather than exploding right here (the BLOCKS section isn't
 * guaranteed to appear before ENTITIES in a well-formed DXF, so the
 * referenced block might not exist yet at this point in a single
 * forward pass), this just emits a plain INSERT entity at identity
 * placement (point 0,0,0, angle 0, default scale 1) referencing the
 * same block name -- the INSERT-explosion post-pass dwg_read_dxf
 * already runs after the whole file is loaded picks this up
 * automatically, reusing that already-robust mechanism instead of
 * duplicating it here.
 */
static void dxf_read_dimension(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity = NULL;
    int code;
    char value[DXF_LINE_MAX];
    char block_name[DWG_BLOCK_NAME_MAX];

    block_name[0] = '\0';

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            break;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (code == 2)
        {
            strncpy(block_name, value, DWG_BLOCK_NAME_MAX - 1UL);
            block_name[DWG_BLOCK_NAME_MAX - 1UL] = '\0';
            continue;
        }

        if (entity != NULL)
            (void)dxf_apply_common_code(entity, code, value);
        else if (block_name[0] != '\0')
        {
            /* layer/color codes may appear before or after group 2 --
               once the block name is known, retroactively create the
               INSERT and let dxf_apply_common_code catch up on this
               and any further common codes. */
            entity = dwg_add_insert(hDwg, block_name, 0.0, 0.0, 0.0, 0.0);
            (void)dxf_apply_common_code(entity, code, value);
        }
    }

    if (entity == NULL && block_name[0] != '\0')
        (void)dwg_add_insert(hDwg, block_name, 0.0, 0.0, 0.0, 0.0);
}

/*
 * Real HATCH entities can carry many separate boundary paths (island/
 * hole cutouts, small accent regions, disjoint pieces -- confirmed
 * against ROTATORIO.dxf: 20 paths on one HATCH, 14 on another).
 * DWG_HATCH only models a single boundary loop, so this reader has to
 * pick which path(s) to keep. Two "pick the best single path"
 * heuristics were tried and reverted after each produced a real,
 * visible regression on ROTATORIO.dxf (connecting vertices across
 * unrelated paths into a self-intersecting shape; picking a large
 * unrelated "container" path instead of the real fill region).
 *
 * The actual reliable signal, confirmed against real data: a path with
 * a nonzero BULGE (group 42) on any vertex is a genuinely curved
 * boundary -- and in this file, that's EXACTLY the two paths (of 20)
 * that together form the real pie-wedge fill (confirmed by Arturo's
 * own reference screenshot: two quarter-circle pieces making up the
 * bottom half). The many plain straight-edged paths (accent rectangles,
 * hole cutouts, tick marks) never have bulge. So: emit ONE HATCH entity
 * per bulge-bearing path (there can be more than one -- each becomes
 * its own separate filled shape, all sharing this HATCH's layer/color/
 * pattern/scale/angle/solid), and fall back to the old "first path
 * only" behavior if no path in this entity has any bulge at all (a
 * plain HATCH with no curved boundary, where "first path" is a
 * reasonable simple default).
 */
#define DXF_HATCH_MAX_PATH_PTS 64UL

/* Emits hatch entity `he`'s boundary points (with per-vertex bulge, so
   dwg_render.c can tessellate the curved segments instead of drawing
   straight chords -- confirmed missing by Arturo: "falta el hatch
   arco"). pts[k] is (x,y,bulge); bulge applies to the segment FROM
   vertex k TO vertex k+1, same convention as LWPOLYLINE/POLYLINE. */
static void dxf_hatch_emit_path(HENTITY he, double pts[][3], unsigned long count)
{
    unsigned long k;
    for (k = 0UL; k < count; k++)
    {
        HVERTEX v = dwg_hatch_add_boundary_point(he, pts[k][0], pts[k][1], 0.0);
        if (v != NULL && pts[k][2] != 0.0)
            dwg_vertex_set_bulge(v, pts[k][2]);
    }
}

#define DXF_HATCH_MAX_EXTRA_ENTITIES 16UL

static void dxf_read_hatch(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    char pattern[DXF_LINE_MAX];
    double scale = 1.0, angle = 0.0;
    int solid = 0;
    double pending_x = 0.0;
    int have_pending_x = 0;
    int path_starts_seen = 0;

    double cur_pts[DXF_HATCH_MAX_PATH_PTS][3];
    unsigned long cur_count = 0UL;
    int cur_has_bulge = 0;

    double first_pts[DXF_HATCH_MAX_PATH_PTS][3];
    unsigned long first_count = 0UL;

    /* every hatch entity created for a bulge-bearing path beyond the
       first (which reuses `entity` itself) -- kept so pattern/scale/
       angle/solid/color, all of which get applied only after the WHOLE
       entity is parsed (see gradient_aci below), can be applied to
       every one of them, not just the placeholder. */
    HENTITY extra_entities[DXF_HATCH_MAX_EXTRA_ENTITIES];
    unsigned long extra_count = 0UL;
    int bulge_paths_emitted = 0;

    const char *layer_name = "0";
    /* GRADIENT hatch (group 450/470): real per-stop colors given as
       ACI (group 63) + true-color (group 421) pairs, 2 stops (start/
       end) -- confirmed real in ROTATORIO.dxf (blue -> yellow), and
       the group codes for it always come AFTER all boundary path data
       in a real file, which is why gradient_aci can't be applied at
       entity-creation time (mid-loop, for the 2nd+ bulge path) and has
       to wait until the whole entity has been read, then get applied
       to every entity this call created. This engine has no gradient-
       fill rendering, so as a solid-color approximation this keeps the
       LAST (end) stop's ACI -- already a real, exact color this
       engine's palette renders correctly, rather than leaving the fill
       at its BYLAYER color (plain white for this file -- confirmed
       missing by Arturo: "falta ... color"). */
    long gradient_aci = -1L;

    pattern[0] = '\0';

    /* placeholder just to capture layer/color via dxf_apply_common_code
       as codes stream by -- becomes the real entity for the "no bulge
       path found" fallback case, discarded (0 boundary points, which
       dwg_render.c's draw_hatch already ignores) otherwise. */
    entity = dwg_add_hatch(hDwg, "", 0.0, 1.0, DWG_FALSE);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            break;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 2:
            strncpy(pattern, value, DXF_LINE_MAX - 1UL);
            pattern[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 70: solid = atoi(value); break;
        case 41: scale = atof(value); break;
        case 52: angle = atof(value); break;
        case 63: gradient_aci = atol(value); break;
        case 92:
            /* a new path starts -- flush whatever was accumulating */
            if (cur_has_bulge && cur_count >= 3UL)
            {
                HENTITY he;

                if (bulge_paths_emitted == 0)
                    he = entity; /* reuse the placeholder for the first one */
                else
                {
                    he = dwg_add_hatch(hDwg, "", 0.0, 1.0, DWG_FALSE);
                    dwg_entity_put_layer(he, layer_name);
                    if (extra_count < DXF_HATCH_MAX_EXTRA_ENTITIES)
                        extra_entities[extra_count++] = he;
                }
                dxf_hatch_emit_path(he, cur_pts, cur_count);
                bulge_paths_emitted++;
            }
            else if (path_starts_seen == 1 && cur_count >= 3UL)
            {
                unsigned long k;
                for (k = 0UL; k < cur_count && k < DXF_HATCH_MAX_PATH_PTS; k++)
                {
                    first_pts[k][0] = cur_pts[k][0];
                    first_pts[k][1] = cur_pts[k][1];
                    first_pts[k][2] = cur_pts[k][2];
                }
                first_count = k;
            }
            cur_count = 0UL;
            cur_has_bulge = 0;
            path_starts_seen++;
            break;
        case 10:
            /* strictly requires path_starts_seen >= 1: a HATCH's own
               group 10/20/30 "elevation point" appears BEFORE the
               first path's own 92 code -- treating that as a boundary
               vertex prepends a spurious point at the hatch's origin,
               usually far from the real boundary (a real bug this
               reader hit and fixed: a long dart-shaped sliver
               connecting the two). */
            if (path_starts_seen >= 1)
            {
                pending_x = atof(value);
                have_pending_x = 1;
            }
            break;
        case 20:
            if (path_starts_seen >= 1 && have_pending_x)
            {
                have_pending_x = 0;
                if (cur_count < DXF_HATCH_MAX_PATH_PTS)
                {
                    cur_pts[cur_count][0] = pending_x;
                    cur_pts[cur_count][1] = atof(value);
                    cur_pts[cur_count][2] = 0.0;
                    cur_count++;
                }
            }
            break;
        case 42:
            /* bulge applies to the vertex just added (group 42 always
               follows that vertex's own 10/20 pair, before the next
               vertex's 10). */
            if (atof(value) != 0.0 && cur_count > 0UL)
            {
                cur_pts[cur_count - 1UL][2] = atof(value);
                cur_has_bulge = 1;
            }
            break;
        default: break; /* 91/72/73/93/97/330: implied by the vertex pairs, not tracked separately */
        }

        if (path_starts_seen == 1)
        {
            /* captured lazily so it reflects whatever
               dxf_apply_common_code has applied to `entity` so far --
               real files put group 8 before the boundary data. */
            layer_name = dwg_entity_get_layer(entity);
        }
    }

    /* flush whatever path was accumulating when the entity ended */
    if (cur_has_bulge && cur_count >= 3UL)
    {
        HENTITY he;

        if (bulge_paths_emitted == 0)
            he = entity;
        else
        {
            he = dwg_add_hatch(hDwg, "", 0.0, 1.0, DWG_FALSE);
            dwg_entity_put_layer(he, layer_name);
            if (extra_count < DXF_HATCH_MAX_EXTRA_ENTITIES)
                extra_entities[extra_count++] = he;
        }
        dxf_hatch_emit_path(he, cur_pts, cur_count);
        bulge_paths_emitted++;
    }
    else if (path_starts_seen <= 1 && cur_count >= 3UL)
    {
        unsigned long k;
        for (k = 0UL; k < cur_count && k < DXF_HATCH_MAX_PATH_PTS; k++)
        {
            first_pts[k][0] = cur_pts[k][0];
            first_pts[k][1] = cur_pts[k][1];
            first_pts[k][2] = cur_pts[k][2];
        }
        first_count = k;
    }

    if (bulge_paths_emitted == 0)
        dxf_hatch_emit_path(entity, first_pts, first_count);

    /* apply pattern/scale/angle/solid/color to every entity this call
       created -- `entity` (always) plus any extras (2nd+ bulge path). */
    {
        unsigned long k;
        HENTITY all[DXF_HATCH_MAX_EXTRA_ENTITIES + 1UL];
        unsigned long all_count = 0UL;

        all[all_count++] = entity;
        for (k = 0UL; k < extra_count; k++)
            all[all_count++] = extra_entities[k];

        for (k = 0UL; k < all_count; k++)
        {
            dwg_hatch_set_pattern(all[k], pattern);
            dwg_hatch_set_scale(all[k], scale);
            dwg_hatch_set_angle(all[k], angle);
            dwg_hatch_set_solid(all[k], solid != 0 ? DWG_TRUE : DWG_FALSE);
            if (gradient_aci >= 0L)
                dwg_entity_put_color(all[k], (unsigned short)gradient_aci);
        }
    }
}

static void dxf_read_solid(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x1 = 0.0, y1 = 0.0, z1 = 0.0;
    double x2 = 0.0, y2 = 0.0, z2 = 0.0;
    double x3 = 0.0, y3 = 0.0, z3 = 0.0;
    double x4 = 0.0, y4 = 0.0, z4 = 0.0;

    entity = dwg_add_solid(hDwg, 0.0,0.0,0.0, 0.0,0.0,0.0, 0.0,0.0,0.0, 0.0,0.0,0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x1 = atof(value); break;
        case 20: y1 = atof(value); break;
        case 30: z1 = atof(value); break;
        case 11: x2 = atof(value); break;
        case 21: y2 = atof(value); break;
        case 31: z2 = atof(value); break;
        case 12: x3 = atof(value); break;
        case 22: y3 = atof(value); break;
        case 32: z3 = atof(value); break;
        case 13: x4 = atof(value); break;
        case 23: y4 = atof(value); break;
        case 33: z4 = atof(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_SOLID3D *s = (DWG_SOLID3D *)entity->geometry;
        s->p1.x = x1; s->p1.y = y1; s->p1.z = z1;
        s->p2.x = x2; s->p2.y = y2; s->p2.z = z2;
        s->p3.x = x3; s->p3.y = y3; s->p3.z = z3;
        s->p4.x = x4; s->p4.y = y4; s->p4.z = z4;
    }
}

static void dxf_read_face(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double x1 = 0.0, y1 = 0.0, z1 = 0.0;
    double x2 = 0.0, y2 = 0.0, z2 = 0.0;
    double x3 = 0.0, y3 = 0.0, z3 = 0.0;
    double x4 = 0.0, y4 = 0.0, z4 = 0.0;
    int flags = 0;

    entity = dwg_add_face(hDwg, 0.0,0.0,0.0, 0.0,0.0,0.0, 0.0,0.0,0.0, 0.0,0.0,0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 10: x1 = atof(value); break;
        case 20: y1 = atof(value); break;
        case 30: z1 = atof(value); break;
        case 11: x2 = atof(value); break;
        case 21: y2 = atof(value); break;
        case 31: z2 = atof(value); break;
        case 12: x3 = atof(value); break;
        case 22: y3 = atof(value); break;
        case 32: z3 = atof(value); break;
        case 13: x4 = atof(value); break;
        case 23: y4 = atof(value); break;
        case 33: z4 = atof(value); break;
        case 70: flags = atoi(value); break;
        default: break;
        }
    }

    if (entity != NULL && entity->geometry != NULL)
    {
        DWG_FACE3D *f = (DWG_FACE3D *)entity->geometry;
        f->p1.x = x1; f->p1.y = y1; f->p1.z = z1;
        f->p2.x = x2; f->p2.y = y2; f->p2.z = z2;
        f->p3.x = x3; f->p3.y = y3; f->p3.z = z3;
        f->p4.x = x4; f->p4.y = y4; f->p4.z = z4;
        f->edge_flags = (unsigned short)flags;
    }
}

static void dxf_read_leader(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    double arrow_size = 1.0, text_height = 2.5;
    int spline = 0;
    char text[DWG_LEADER_TEXT_MAX];
    double pending_x = 0.0, pending_y = 0.0;
    int pending_stage = 0; /* 0=none, 1=have x, 2=have x+y */

    text[0] = '\0';

    entity = dwg_add_leader(hDwg);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 40: arrow_size = atof(value); break;
        case 41: text_height = atof(value); break;
        case 72: spline = atoi(value); break;
        case 3:
            strncpy(text, value, DWG_LEADER_TEXT_MAX - 1UL);
            text[DWG_LEADER_TEXT_MAX - 1UL] = '\0';
            break;
        case 10: pending_x = atof(value); pending_stage = 1; break;
        case 20: pending_y = atof(value); pending_stage = 2; break;
        case 30:
            if (pending_stage == 2)
            {
                dwg_leader_add_vertex(entity, pending_x, pending_y, atof(value));
                pending_stage = 0;
            }
            break;
        default: break; /* 76: vertex count, implied by how many triples arrive */
        }
    }

    dwg_leader_set_arrow_size(entity, arrow_size);
    dwg_leader_set_text_height(entity, text_height);
    dwg_leader_set_spline(entity, spline != 0 ? DWG_TRUE : DWG_FALSE);
    dwg_leader_set_text(entity, text);
}

static void dxf_read_insert(HDWG hDwg, DXF_READER *reader)
{
    HENTITY entity;
    int code;
    char value[DXF_LINE_MAX];
    char block_name[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0;
    double sx = 1.0, sy = 1.0, sz = 1.0;
    double angle = 0.0;

    block_name[0] = '\0';

    entity = dwg_add_insert(hDwg, "", 0.0, 0.0, 0.0, 0.0);

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        if (dxf_apply_common_code(entity, code, value))
            continue;

        switch (code)
        {
        case 2:
            strncpy(block_name, value, DXF_LINE_MAX - 1UL);
            block_name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 10: x = atof(value); break;
        case 20: y = atof(value); break;
        case 30: z = atof(value); break;
        case 41: sx = atof(value); break;
        case 42: sy = atof(value); break;
        case 43: sz = atof(value); break;
        case 50: angle = atof(value); break;
        default: break;
        }
    }

    dwg_insert_set_block_name(entity, block_name);
    dwg_insert_set_point(entity, x, y, z);
    dwg_insert_set_scale(entity, sx, sy, sz);
    dwg_insert_set_angle(entity, angle);
}

/*
 * Dispatches one entity record by DXF type name. Shared by the main
 * ENTITIES section and the per-BLOCK entity lists in the BLOCKS section
 * (a block's own entities use the exact same record shapes). Returns 1
 * if the type was recognized and consumed, 0 if the caller should skip
 * it via dxf_skip_unknown_entity.
 */
static int dxf_read_one_entity(HDWG hDwg, DXF_READER *reader, const char *type_name)
{
    if (strcmp(type_name, "POINT") == 0)
        dxf_read_point(hDwg, reader);
    else if (strcmp(type_name, "LINE") == 0)
        dxf_read_line(hDwg, reader);
    else if (strcmp(type_name, "CIRCLE") == 0)
        dxf_read_circle(hDwg, reader);
    else if (strcmp(type_name, "ARC") == 0)
        dxf_read_arc(hDwg, reader);
    else if (strcmp(type_name, "POLYLINE") == 0)
        dxf_read_polyline(hDwg, reader);
    else if (strcmp(type_name, "LWPOLYLINE") == 0)
        dxf_read_lwpolyline(hDwg, reader);
    else if (strcmp(type_name, "TEXT") == 0)
        dxf_read_text(hDwg, reader);
    else if (strcmp(type_name, "INSERT") == 0)
        dxf_read_insert(hDwg, reader);
    else if (strcmp(type_name, "MTEXT") == 0)
        dxf_read_mtext(hDwg, reader);
    else if (strcmp(type_name, "HATCH") == 0)
        dxf_read_hatch(hDwg, reader);
    else if (strcmp(type_name, "SOLID") == 0)
        dxf_read_solid(hDwg, reader);
    else if (strcmp(type_name, "3DFACE") == 0)
        dxf_read_face(hDwg, reader);
    else if (strcmp(type_name, "LEADER") == 0)
        dxf_read_leader(hDwg, reader);
    else if (strcmp(type_name, "DIMENSION") == 0)
        dxf_read_dimension(hDwg, reader);
    else
        return 0;

    return 1;
}

static void dxf_read_entities_section(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code != 0)
            continue;

        if (strcmp(value, "ENDSEC") == 0)
            return;

        if (!dxf_read_one_entity(hDwg, reader, value))
            dxf_skip_unknown_entity(reader);
    }
}

static void dxf_read_blocks_section(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];
    char name[DXF_LINE_MAX];
    double x = 0.0, y = 0.0, z = 0.0;
    HBLOCK block;
    HENTITY entity;
    HENTITY next_entity;
    unsigned long before, after, i;

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code != 0)
            continue;

        if (strcmp(value, "ENDSEC") == 0)
            return;

        if (strcmp(value, "BLOCK") != 0)
        {
            dxf_skip_unknown_entity(reader);
            continue;
        }

        name[0] = '\0';
        x = y = z = 0.0;

        for (;;)
        {
            if (!dxf_next(reader, &code, value))
                return;

            if (code == 0)
            {
                dxf_pushback(reader, code, value);
                break;
            }

            switch (code)
            {
            case 2:
                strncpy(name, value, DXF_LINE_MAX - 1UL);
                name[DXF_LINE_MAX - 1UL] = '\0';
                break;
            case 10: x = atof(value); break;
            case 20: y = atof(value); break;
            case 30: z = atof(value); break;
            default: break;
            }
        }

        block = dwg_document_add_block(hDwg, name);
        if (block != NULL)
            dwg_block_set_base(block, x, y, z);

        /* Las entidades del bloque se leen con las mismas funciones que
           las entidades normales (agregan al documento), y luego se
           trasladan a la lista propia del bloque. */
        before = dwg_document_entity_count(hDwg);

        for (;;)
        {
            if (!dxf_next(reader, &code, value))
                return;

            if (code != 0)
                continue;

            if (strcmp(value, "ENDBLK") == 0)
            {
                for (;;)
                {
                    if (!dxf_next(reader, &code, value))
                        return;

                    if (code == 0)
                    {
                        dxf_pushback(reader, code, value);
                        break;
                    }
                }
                break;
            }

            if (!dxf_read_one_entity(hDwg, reader, value))
                dxf_skip_unknown_entity(reader);
        }

        if (block != NULL)
        {
            after = dwg_document_entity_count(hDwg);
            entity = dwg_document_first_entity(hDwg);

            for (i = 0UL; i < before && entity != NULL; i++)
                entity = dwg_document_next_entity(entity);

            for (i = before; i < after && entity != NULL; i++)
            {
                next_entity = dwg_document_next_entity(entity);
                dwg_document_detach_entity(hDwg, entity);
                dwg_block_attach_entity(block, entity);
                entity = next_entity;
            }
        }
    }
}

static void dxf_read_layer_table_entry(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];
    char name[DXF_LINE_MAX];
    int color = 7;
    HLAYER layer;

    name[0] = '\0';

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        switch (code)
        {
        case 2:
            strncpy(name, value, DXF_LINE_MAX - 1UL);
            name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 62:
            color = atoi(value);
            break;
        default:
            break;
        }
    }

    if (name[0] != '\0')
    {
        layer = dwg_document_add_layer(hDwg, name);
        if (layer != NULL)
            dwg_layer_set_color(layer, (unsigned short)(color < 0 ? -color : color));
    }
}

static void dxf_read_linetype_table_entry(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];
    char name[DXF_LINE_MAX];
    char descr[DXF_LINE_MAX];
    HLINETYPE linetype;

    name[0] = '\0';
    descr[0] = '\0';

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        switch (code)
        {
        case 2:
            strncpy(name, value, DXF_LINE_MAX - 1UL);
            name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 3:
            strncpy(descr, value, DXF_LINE_MAX - 1UL);
            descr[DXF_LINE_MAX - 1UL] = '\0';
            break;
        default:
            break;
        }
    }

    if (name[0] != '\0')
    {
        linetype = dwg_document_add_linetype(hDwg, name);
        if (linetype != NULL)
            dwg_linetype_set_descr(linetype, descr);
    }
}

static void dxf_read_style_table_entry(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];
    char name[DXF_LINE_MAX];
    char font[DXF_LINE_MAX];
    char ttf[DXF_LINE_MAX];
    double height = DWG_STYLE_DEFAULT_HEIGHT;
    double width_factor = DWG_STYLE_DEFAULT_WIDTH;
    double oblique = 0.0;
    long flags = 0L;
    HSTYLE style;

    name[0] = '\0';
    font[0] = '\0';
    ttf[0] = '\0';

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0)
        {
            dxf_pushback(reader, code, value);
            break;
        }

        switch (code)
        {
        case 2:
            strncpy(name, value, DXF_LINE_MAX - 1UL);
            name[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 3:
            strncpy(font, value, DXF_LINE_MAX - 1UL);
            font[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 4:
            strncpy(ttf, value, DXF_LINE_MAX - 1UL);
            ttf[DXF_LINE_MAX - 1UL] = '\0';
            break;
        case 40: height = atof(value); break;
        case 41: width_factor = atof(value); break;
        case 50: oblique = atof(value); break;
        case 71: flags = atol(value); break;
        default: break;
        }
    }

    if (name[0] != '\0')
    {
        style = dwg_document_add_style(hDwg, name);
        if (style != NULL)
        {
            dwg_style_set_font(style, font);
            dwg_style_set_ttf_name(style, ttf);
            dwg_style_set_height(style, height);
            dwg_style_set_width_factor(style, width_factor);
            dwg_style_set_oblique(style, oblique);
            dwg_style_set_backward(style, (flags & 2L) != 0L ? DWG_TRUE : DWG_FALSE);
            dwg_style_set_upside_down(style, (flags & 4L) != 0L ? DWG_TRUE : DWG_FALSE);
        }
    }
}

static void dxf_read_tables_section(HDWG hDwg, DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code != 0)
            continue;

        if (strcmp(value, "ENDSEC") == 0)
            return;

        if (strcmp(value, "LAYER") == 0)
            dxf_read_layer_table_entry(hDwg, reader);
        else if (strcmp(value, "LTYPE") == 0)
            dxf_read_linetype_table_entry(hDwg, reader);
        else if (strcmp(value, "STYLE") == 0)
            dxf_read_style_table_entry(hDwg, reader);
    }
}

static void dxf_skip_section(DXF_READER *reader)
{
    int code;
    char value[DXF_LINE_MAX];

    for (;;)
    {
        if (!dxf_next(reader, &code, value))
            return;

        if (code == 0 && strcmp(value, "ENDSEC") == 0)
            return;
    }
}

HDWG dwg_read_dxf(const char *path, DWG_IO_RESULT *result)
{
    HDWG hDwg;
    DXF_READER reader;
    int code;
    char value[DXF_LINE_MAX];

    if (result != NULL)
        *result = DWG_IO_OK;

    if (path == NULL)
    {
        if (result != NULL)
            *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    reader.fp = fopen(path, "r");
    if (reader.fp == NULL)
    {
        if (result != NULL)
            *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    reader.has_pending = 0;

    hDwg = dwg_document_create();
    if (hDwg == NULL)
    {
        fclose(reader.fp);
        if (result != NULL)
            *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    for (;;)
    {
        if (!dxf_next(&reader, &code, value))
            break;

        if (code != 0)
            continue;

        if (strcmp(value, "EOF") == 0)
            break;

        if (strcmp(value, "SECTION") != 0)
            continue;

        /* Leer el nombre de la seccion (codigo 2) */
        if (!dxf_next(&reader, &code, value))
            break;

        if (code != 2)
        {
            dxf_skip_section(&reader);
            continue;
        }

        if (strcmp(value, "TABLES") == 0)
            dxf_read_tables_section(hDwg, &reader);
        else if (strcmp(value, "BLOCKS") == 0)
            dxf_read_blocks_section(hDwg, &reader);
        else if (strcmp(value, "ENTITIES") == 0)
            dxf_read_entities_section(hDwg, &reader);
        else
            dxf_skip_section(&reader);
    }

    fclose(reader.fp);

    /*
     * Explode every INSERT into real, transformed copies of its
     * referenced block's entities (dwg_entity_explode already does the
     * scale/rotate/move math -- see dwg_transform.h). The renderer
     * (dwg_render.c) has no concept of block references at all, so
     * without this any INSERT (a title-block/border reference is a
     * common real case -- exactly what was missing from ROTATORIO.dxf's
     * render) was silently invisible. Snapshot the entity list first
     * since dwg_entity_explode appends to it -- iterating live would
     * walk into the newly-created copies too (and if one of those
     * happened to itself be an INSERT, which explode's own recursive
     * block content can produce, it would explode it a second time).
     * The original INSERT entities are left in place, unexploded and
     * harmless: the renderer already silently skips any entity type
     * it doesn't know how to draw.
     */
    {
        HENTITY e, next;
        unsigned long count, i;
        HENTITY *snapshot;

        count = dwg_document_entity_count(hDwg);
        snapshot = (HENTITY *)malloc(count * sizeof(HENTITY));
        if (snapshot != NULL)
        {
            i = 0UL;
            for (e = dwg_document_first_entity(hDwg); e != NULL && i < count; e = next)
            {
                next = dwg_document_next_entity(e);
                snapshot[i++] = e;
            }
            for (i = 0UL; i < count; i++)
            {
                if (dwg_entity_get_type(snapshot[i]) == DWG_ENTITY_INSERT)
                    (void)dwg_entity_explode(hDwg, snapshot[i]);
            }
            free(snapshot);
        }
    }

    /*
     * Resolve BYLAYER (256) / BYBLOCK (0) entity colors to their real
     * layer color, same "apply_color from the resolved layer" step
     * dwg_r2000_entity_reader.c/dwg_r1314_entity_reader.c already do at
     * decode time -- DXF entities almost always omit group 62 (relying
     * on BYLAYER by convention), and the renderer draws an entity's own
     * raw color with no layer lookup of its own, so without this every
     * DXF entity rendered as the same fallback color regardless of its
     * real per-layer color (Arturo: "en la edicion de dxf no salen los
     * colores"). Done as a post-pass after the whole file is read (and
     * after INSERT explosion above, so exploded block copies get
     * resolved too) rather than inline while parsing ENTITIES, since a
     * real DXF's TABLES section isn't guaranteed to precede ENTITIES.
     */
    {
        HENTITY e;
        for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
        {
            unsigned short color = dwg_entity_get_color(e);
            if (color == 0U || color == 256U)
            {
                HLAYER layer = dwg_document_get_layer(hDwg, dwg_entity_get_layer(e));
                if (layer != NULL)
                    dwg_entity_put_color(e, dwg_layer_get_color(layer));
            }
        }
    }

    return hDwg;
}
