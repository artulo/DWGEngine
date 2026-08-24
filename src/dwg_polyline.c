#include <stdlib.h>

#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_entity.h"
#include "dwg_document.h"

HPOLYLINE dwg_polyline_from_entity(HENTITY entity)
{
    if (entity == NULL)
        return NULL;

    if (entity->type != DWG_ENTITY_POLYLINE)
        return NULL;

    return (HPOLYLINE)entity->geometry;
}

static void dwg_polyline_append_vertex(HPOLYLINE polyline,
                                       HVERTEX vertex)
{
    if (polyline->first_vertex == NULL)
    {
        polyline->first_vertex = vertex;
        polyline->last_vertex = vertex;
    }
    else
    {
        vertex->prev = polyline->last_vertex;
        polyline->last_vertex->next = vertex;
        polyline->last_vertex = vertex;
    }

    polyline->vertex_count++;
}

HVERTEX dwg_polyline_add_vertex(HPOLYLINE polyline,
                                 double x, double y, double z)
{
    HVERTEX vertex;

    if (polyline == NULL)
        return NULL;

    vertex = dwg_vertex_create(x, y, z);
    if (vertex == NULL)
        return NULL;

    dwg_polyline_append_vertex(polyline, vertex);
    return vertex;
}

HVERTEX dwg_polyline_add_vertex2(HPOLYLINE polyline,
                                  double x, double y, double z,
                                  double bulge,
                                  double start_width,
                                  double end_width)
{
    HVERTEX vertex;

    vertex = dwg_polyline_add_vertex(polyline, x, y, z);
    if (vertex == NULL)
        return NULL;

    vertex->bulge = bulge;
    vertex->start_width = start_width;
    vertex->end_width = end_width;

    return vertex;
}

unsigned long dwg_polyline_vertex_count(HPOLYLINE polyline)
{
    return polyline != NULL ? polyline->vertex_count : 0UL;
}

HVERTEX dwg_polyline_first_vertex(HPOLYLINE polyline)
{
    return polyline != NULL ? polyline->first_vertex : NULL;
}

HVERTEX dwg_polyline_last_vertex(HPOLYLINE polyline)
{
    return polyline != NULL ? polyline->last_vertex : NULL;
}

HVERTEX dwg_polyline_next_vertex(HVERTEX vertex)
{
    return vertex != NULL ? vertex->next : NULL;
}

HVERTEX dwg_polyline_prev_vertex(HVERTEX vertex)
{
    return vertex != NULL ? vertex->prev : NULL;
}

long dwg_polyline_set_closed(HPOLYLINE polyline, long closed)
{
    if (polyline == NULL)
        return 0L;

    if (closed)
        polyline->flags |= DWG_POLYLINE_CLOSED;
    else
        polyline->flags &= ~DWG_POLYLINE_CLOSED;

    return 1L;
}

long dwg_polyline_is_closed(HPOLYLINE polyline)
{
    if (polyline == NULL)
        return 0L;

    return (polyline->flags & DWG_POLYLINE_CLOSED) != 0UL;
}

void dwg_polyline_set_elevation(HPOLYLINE polyline, double elevation)
{
    if (polyline != NULL)
        polyline->elevation = elevation;
}

double dwg_polyline_get_elevation(HPOLYLINE polyline)
{
    return polyline != NULL ? polyline->elevation : 0.0;
}
