#include <stdlib.h>
#include "dwg_vertex.h"

HVERTEX dwg_vertex_create(double x, double y, double z)
{
    HVERTEX vertex;

    vertex = (HVERTEX)malloc(sizeof(DWG_VERTEX));
    if (vertex == NULL)
        return NULL;

    vertex->point.x = x;
    vertex->point.y = y;
    vertex->point.z = z;
    vertex->bulge = 0.0;
    vertex->start_width = 0.0;
    vertex->end_width = 0.0;
    vertex->flags = 0UL;
    vertex->next = NULL;
    vertex->prev = NULL;

    return vertex;
}

void dwg_vertex_destroy(HVERTEX vertex)
{
    if (vertex != NULL)
        free(vertex);
}

void dwg_vertex_set_point(HVERTEX vertex, double x, double y, double z)
{
    if (vertex == NULL)
        return;

    vertex->point.x = x;
    vertex->point.y = y;
    vertex->point.z = z;
}

void dwg_vertex_get_point(HVERTEX vertex,
                          double *x, double *y, double *z)
{
    if (vertex == NULL)
        return;

    if (x != NULL) *x = vertex->point.x;
    if (y != NULL) *y = vertex->point.y;
    if (z != NULL) *z = vertex->point.z;
}

void dwg_vertex_set_bulge(HVERTEX vertex, double bulge)
{
    if (vertex != NULL)
        vertex->bulge = bulge;
}

double dwg_vertex_get_bulge(HVERTEX vertex)
{
    return vertex != NULL ? vertex->bulge : 0.0;
}

void dwg_vertex_set_width(HVERTEX vertex,
                          double start_width,
                          double end_width)
{
    if (vertex == NULL)
        return;

    vertex->start_width = start_width;
    vertex->end_width = end_width;
}
