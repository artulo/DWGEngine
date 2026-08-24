#ifndef DWG_VERTEX_H
#define DWG_VERTEX_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _DWG_VERTEX
{
    DWG_POINT3D point;
    double bulge;
    double start_width;
    double end_width;
    unsigned long flags;
    DWG_VERTEX *next;
    DWG_VERTEX *prev;
};

HVERTEX dwg_vertex_create(double x, double y, double z);
void dwg_vertex_destroy(HVERTEX vertex);

void dwg_vertex_set_point(HVERTEX vertex, double x, double y, double z);
void dwg_vertex_get_point(HVERTEX vertex, double *x, double *y, double *z);

void dwg_vertex_set_bulge(HVERTEX vertex, double bulge);
double dwg_vertex_get_bulge(HVERTEX vertex);

void dwg_vertex_set_width(HVERTEX vertex,
                          double start_width, double end_width);

#ifdef __cplusplus
}
#endif

#endif
