#include <stdio.h>

#include "dwg_file_io.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_layer.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_block.h"
#include "dwg_insert.h"
#include "dwg_linetype.h"
#include "dwg_style.h"
#include "dwg_mtext.h"
#include "dwg_hatch.h"
#include "dwg_solid.h"
#include "dwg_leader.h"

static void dxf_write_common(FILE *fp, HENTITY entity)
{
    const char *layer;
    const char *linetype;

    layer = dwg_entity_get_layer(entity);
    if (layer != NULL && layer[0] != '\0')
        fprintf(fp, "8\n%s\n", layer);

    fprintf(fp, "62\n%d\n", (int)dwg_entity_get_color(entity));

    linetype = dwg_entity_get_linetype(entity);
    if (linetype != NULL && linetype[0] != '\0')
        fprintf(fp, "6\n%s\n", linetype);
}

static void dxf_write_point(FILE *fp, HENTITY entity, const DWG_POINT3D *p)
{
    fprintf(fp, "0\nPOINT\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", p->x, p->y, p->z);
}

static void dxf_write_line(FILE *fp, HENTITY entity, const DWG_LINE3D *l)
{
    fprintf(fp, "0\nLINE\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n",
            l->start.x, l->start.y, l->start.z);
    fprintf(fp, "11\n%.10g\n21\n%.10g\n31\n%.10g\n",
            l->end.x, l->end.y, l->end.z);
}

static void dxf_write_circle(FILE *fp, HENTITY entity, const DWG_CIRCLE3D *c)
{
    fprintf(fp, "0\nCIRCLE\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n40\n%.10g\n",
            c->center.x, c->center.y, c->center.z, c->radius);
}

static void dxf_write_arc(FILE *fp, HENTITY entity, const DWG_ARC3D *a)
{
    fprintf(fp, "0\nARC\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n40\n%.10g\n",
            a->center.x, a->center.y, a->center.z, a->radius);
    fprintf(fp, "50\n%.10g\n51\n%.10g\n", a->start_angle, a->end_angle);
}

static void dxf_write_polyline(FILE *fp, HENTITY entity)
{
    HPOLYLINE polyline;
    HVERTEX vertex;
    long flags;

    polyline = dwg_polyline_from_entity(entity);
    if (polyline == NULL)
        return;

    flags = dwg_polyline_is_closed(polyline) ? 1L : 0L;

    fprintf(fp, "0\nPOLYLINE\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "66\n1\n");
    fprintf(fp, "30\n%.10g\n", dwg_polyline_get_elevation(polyline));
    fprintf(fp, "70\n%ld\n", flags);

    vertex = dwg_polyline_first_vertex(polyline);
    while (vertex != NULL)
    {
        double x, y, z;

        dwg_vertex_get_point(vertex, &x, &y, &z);

        fprintf(fp, "0\nVERTEX\n");
        if (dwg_entity_get_layer(entity) != NULL && dwg_entity_get_layer(entity)[0] != '\0')
            fprintf(fp, "8\n%s\n", dwg_entity_get_layer(entity));
        fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", x, y, z);

        if (dwg_vertex_get_bulge(vertex) != 0.0)
            fprintf(fp, "42\n%.10g\n", dwg_vertex_get_bulge(vertex));

        vertex = dwg_polyline_next_vertex(vertex);
    }

    fprintf(fp, "0\nSEQEND\n");
}

static void dxf_write_text(FILE *fp, HENTITY entity)
{
    double x, y, z;
    double x0, y0, z0;
    long flags;

    dwg_text_get_point(entity, &x, &y, &z);
    dwg_text_get_point0(entity, &x0, &y0, &z0);

    fprintf(fp, "0\nTEXT\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", x, y, z);
    fprintf(fp, "40\n%.10g\n", dwg_text_get_height(entity));
    fprintf(fp, "1\n%s\n", dwg_text_get_text(entity));
    if (dwg_text_get_style_name(entity)[0] != '\0')
        fprintf(fp, "7\n%s\n", dwg_text_get_style_name(entity));
    fprintf(fp, "50\n%.10g\n", dwg_text_get_angle(entity));
    fprintf(fp, "41\n%.10g\n", dwg_text_get_width_factor(entity));
    fprintf(fp, "51\n%.10g\n", dwg_text_get_oblique(entity));

    flags = 0L;
    if (dwg_text_get_backward(entity))
        flags |= 2L;
    if (dwg_text_get_upside_down(entity))
        flags |= 4L;
    if (flags != 0L)
        fprintf(fp, "71\n%ld\n", flags);

    fprintf(fp, "72\n%d\n", (int)dwg_text_get_align(entity));
    fprintf(fp, "11\n%.10g\n21\n%.10g\n31\n%.10g\n", x0, y0, z0);
}

static void dxf_write_mtext(FILE *fp, HENTITY entity)
{
    double x, y, z;

    dwg_mtext_get_point(entity, &x, &y, &z);

    fprintf(fp, "0\nMTEXT\n");
    dxf_write_common(fp, entity);
    if (dwg_mtext_get_style_name(entity)[0] != '\0')
        fprintf(fp, "7\n%s\n", dwg_mtext_get_style_name(entity));
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", x, y, z);
    fprintf(fp, "40\n%.10g\n", dwg_mtext_get_height(entity));
    fprintf(fp, "41\n%.10g\n", dwg_mtext_get_rect_width(entity));
    fprintf(fp, "44\n%.10g\n", dwg_mtext_get_line_space(entity));
    fprintf(fp, "50\n%.10g\n", dwg_mtext_get_angle(entity));
    fprintf(fp, "71\n%d\n", (int)dwg_mtext_get_attach(entity));
    fprintf(fp, "1\n%s\n", dwg_mtext_get_text(entity));
}

static void dxf_write_hatch(FILE *fp, HENTITY entity)
{
    HVERTEX vertex;
    double x, y, z;

    fprintf(fp, "0\nHATCH\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "2\n%s\n", dwg_hatch_get_pattern(entity));
    fprintf(fp, "70\n%d\n", dwg_hatch_get_solid(entity) ? 1 : 0);
    fprintf(fp, "41\n%.10g\n", dwg_hatch_get_scale(entity));
    fprintf(fp, "52\n%.10g\n", dwg_hatch_get_angle(entity));

    /* Un solo lazo de borde poligonal, sin bulge -- caso simple del
       formato real de boundary paths de DXF (grupos 91/92/72/73/93). */
    fprintf(fp, "91\n1\n92\n2\n72\n0\n73\n1\n93\n%lu\n", dwg_hatch_boundary_count(entity));

    vertex = dwg_hatch_first_boundary_point(entity);
    while (vertex != NULL)
    {
        dwg_vertex_get_point(vertex, &x, &y, &z);
        fprintf(fp, "10\n%.10g\n20\n%.10g\n", x, y);
        vertex = dwg_hatch_next_boundary_point(vertex);
    }
}

static void dxf_write_solid(FILE *fp, HENTITY entity, const DWG_SOLID3D *s)
{
    fprintf(fp, "0\nSOLID\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", s->p1.x, s->p1.y, s->p1.z);
    fprintf(fp, "11\n%.10g\n21\n%.10g\n31\n%.10g\n", s->p2.x, s->p2.y, s->p2.z);
    fprintf(fp, "12\n%.10g\n22\n%.10g\n32\n%.10g\n", s->p3.x, s->p3.y, s->p3.z);
    fprintf(fp, "13\n%.10g\n23\n%.10g\n33\n%.10g\n", s->p4.x, s->p4.y, s->p4.z);
}

static void dxf_write_face(FILE *fp, HENTITY entity, const DWG_FACE3D *f)
{
    fprintf(fp, "0\n3DFACE\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", f->p1.x, f->p1.y, f->p1.z);
    fprintf(fp, "11\n%.10g\n21\n%.10g\n31\n%.10g\n", f->p2.x, f->p2.y, f->p2.z);
    fprintf(fp, "12\n%.10g\n22\n%.10g\n32\n%.10g\n", f->p3.x, f->p3.y, f->p3.z);
    fprintf(fp, "13\n%.10g\n23\n%.10g\n33\n%.10g\n", f->p4.x, f->p4.y, f->p4.z);
    fprintf(fp, "70\n%d\n", (int)f->edge_flags);
}

static void dxf_write_leader(FILE *fp, HENTITY entity)
{
    HVERTEX vertex;
    double x, y, z;

    fprintf(fp, "0\nLEADER\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "40\n%.10g\n", dwg_leader_get_arrow_size(entity));
    fprintf(fp, "72\n%d\n", dwg_leader_get_spline(entity) ? 1 : 0);
    fprintf(fp, "41\n%.10g\n", dwg_leader_get_text_height(entity));
    fprintf(fp, "76\n%lu\n", dwg_leader_vertex_count(entity));

    vertex = dwg_leader_first_vertex(entity);
    while (vertex != NULL)
    {
        dwg_vertex_get_point(vertex, &x, &y, &z);
        fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", x, y, z);
        vertex = dwg_leader_next_vertex(vertex);
    }

    fprintf(fp, "3\n%s\n", dwg_leader_get_text(entity));
}

static void dxf_write_insert(FILE *fp, HENTITY entity)
{
    double x, y, z, sx, sy, sz;

    dwg_insert_get_point(entity, &x, &y, &z);
    dwg_insert_get_scale(entity, &sx, &sy, &sz);

    fprintf(fp, "0\nINSERT\n");
    dxf_write_common(fp, entity);
    fprintf(fp, "2\n%s\n", dwg_insert_get_block_name(entity));
    fprintf(fp, "10\n%.10g\n20\n%.10g\n30\n%.10g\n", x, y, z);
    fprintf(fp, "41\n%.10g\n42\n%.10g\n43\n%.10g\n", sx, sy, sz);
    fprintf(fp, "50\n%.10g\n", dwg_insert_get_angle(entity));
}

static void dxf_write_entity(FILE *fp, HENTITY entity)
{
    /* Entities created via the low-level dwg_document_add_entity/
       dwg_block_add_entity (as opposed to dwg_add_line/dwg_add_circle/
       etc.) may not have their geometry attached yet -- skip rather than
       crash on a NULL entity->geometry. */
    if (entity->geometry == NULL && dwg_entity_get_type(entity) != DWG_ENTITY_INSERT)
        return;

    switch (dwg_entity_get_type(entity))
    {
    case DWG_ENTITY_POINT:
        dxf_write_point(fp, entity, (const DWG_POINT3D *)entity->geometry);
        break;

    case DWG_ENTITY_LINE:
        dxf_write_line(fp, entity, (const DWG_LINE3D *)entity->geometry);
        break;

    case DWG_ENTITY_CIRCLE:
        dxf_write_circle(fp, entity, (const DWG_CIRCLE3D *)entity->geometry);
        break;

    case DWG_ENTITY_ARC:
        dxf_write_arc(fp, entity, (const DWG_ARC3D *)entity->geometry);
        break;

    case DWG_ENTITY_POLYLINE:
        dxf_write_polyline(fp, entity);
        break;

    case DWG_ENTITY_TEXT:
        dxf_write_text(fp, entity);
        break;

    case DWG_ENTITY_INSERT:
        dxf_write_insert(fp, entity);
        break;

    case DWG_ENTITY_MTEXT:
        dxf_write_mtext(fp, entity);
        break;

    case DWG_ENTITY_HATCH:
        dxf_write_hatch(fp, entity);
        break;

    case DWG_ENTITY_SOLID:
        dxf_write_solid(fp, entity, (const DWG_SOLID3D *)entity->geometry);
        break;

    case DWG_ENTITY_FACE:
        dxf_write_face(fp, entity, (const DWG_FACE3D *)entity->geometry);
        break;

    case DWG_ENTITY_LEADER:
        dxf_write_leader(fp, entity);
        break;

    default:
        break;
    }
}

static void dxf_write_blocks(FILE *fp, HDWG hDwg)
{
    HBLOCK block;
    HENTITY entity;
    double x, y, z;

    fprintf(fp, "0\nSECTION\n2\nBLOCKS\n");

    block = dwg_document_first_block(hDwg);
    while (block != NULL)
    {
        dwg_block_get_base(block, &x, &y, &z);

        fprintf(fp, "0\nBLOCK\n2\n%s\n70\n0\n10\n%.10g\n20\n%.10g\n30\n%.10g\n3\n%s\n",
                dwg_block_get_name(block), x, y, z, dwg_block_get_name(block));

        entity = dwg_block_first_entity(block);
        while (entity != NULL)
        {
            dxf_write_entity(fp, entity);
            entity = dwg_block_next_entity(entity);
        }

        fprintf(fp, "0\nENDBLK\n");

        block = dwg_document_next_block(block);
    }

    fprintf(fp, "0\nENDSEC\n");
}

static void dxf_write_layers(FILE *fp, HDWG hDwg)
{
    HLAYER layer;
    HLINETYPE linetype;
    HSTYLE style;
    unsigned long count;

    fprintf(fp, "0\nSECTION\n2\nTABLES\n");

    count = dwg_document_linetype_count(hDwg);
    fprintf(fp, "0\nTABLE\n2\nLTYPE\n70\n%lu\n", count);

    linetype = dwg_document_first_linetype(hDwg);
    while (linetype != NULL)
    {
        fprintf(fp, "0\nLTYPE\n2\n%s\n70\n0\n3\n%s\n72\n65\n73\n0\n40\n0.0\n",
                dwg_linetype_get_name(linetype),
                dwg_linetype_get_descr(linetype));

        linetype = dwg_document_next_linetype(linetype);
    }

    fprintf(fp, "0\nENDTAB\n");

    count = dwg_document_style_count(hDwg);
    fprintf(fp, "0\nTABLE\n2\nSTYLE\n70\n%lu\n", count);

    style = dwg_document_first_style(hDwg);
    while (style != NULL)
    {
        fprintf(fp, "0\nSTYLE\n2\n%s\n70\n0\n40\n%.10g\n41\n%.10g\n50\n%.10g\n71\n%d\n3\n%s\n4\n%s\n",
                dwg_style_get_name(style),
                dwg_style_get_height(style),
                dwg_style_get_width_factor(style),
                dwg_style_get_oblique(style),
                (dwg_style_get_backward(style) ? 2 : 0) | (dwg_style_get_upside_down(style) ? 4 : 0),
                dwg_style_get_font(style),
                dwg_style_get_ttf_name(style));

        style = dwg_document_next_style(style);
    }

    fprintf(fp, "0\nENDTAB\n");

    count = dwg_document_layer_count(hDwg);
    fprintf(fp, "0\nTABLE\n2\nLAYER\n70\n%lu\n", count);

    layer = dwg_document_first_layer(hDwg);
    while (layer != NULL)
    {
        fprintf(fp, "0\nLAYER\n2\n%s\n70\n0\n62\n%d\n6\nCONTINUOUS\n",
                dwg_layer_get_name(layer),
                (int)dwg_layer_get_color(layer));

        layer = dwg_document_next_layer(layer);
    }

    fprintf(fp, "0\nENDTAB\n");
    fprintf(fp, "0\nENDSEC\n");
}

DWG_IO_RESULT dwg_write_dxf(HDWG hDwg, const char *path)
{
    FILE *fp;
    HENTITY entity;

    if (hDwg == NULL || path == NULL)
        return DWG_IO_ERROR_OPEN;

    fp = fopen(path, "w");
    if (fp == NULL)
        return DWG_IO_ERROR_OPEN;

    fprintf(fp, "0\nSECTION\n2\nHEADER\n0\nENDSEC\n");

    dxf_write_layers(fp, hDwg);

    dxf_write_blocks(fp, hDwg);

    fprintf(fp, "0\nSECTION\n2\nENTITIES\n");

    entity = dwg_document_first_entity(hDwg);
    while (entity != NULL)
    {
        dxf_write_entity(fp, entity);

        entity = dwg_document_next_entity(entity);
    }

    fprintf(fp, "0\nENDSEC\n0\nEOF\n");

    fclose(fp);

    return DWG_IO_OK;
}
