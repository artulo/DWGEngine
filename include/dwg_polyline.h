#ifndef DWG_POLYLINE_H
#define DWG_POLYLINE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_POLYLINE_CLOSED 0x0001UL

struct _DWG_POLYLINE
{
    HENTITY entity;
    HVERTEX first_vertex;
    HVERTEX last_vertex;
    unsigned long vertex_count;
    unsigned long flags;
    double elevation;
    double thickness;
    double start_width;
    double end_width;
};

HPOLYLINE dwg_polyline_from_entity(HENTITY entity);

HVERTEX dwg_polyline_add_vertex(HPOLYLINE polyline,
                                 double x, double y, double z);

HVERTEX dwg_polyline_add_vertex2(HPOLYLINE polyline,
                                  double x, double y, double z,
                                  double bulge,
                                  double start_width,
                                  double end_width);

unsigned long dwg_polyline_vertex_count(HPOLYLINE polyline);
HVERTEX dwg_polyline_first_vertex(HPOLYLINE polyline);
HVERTEX dwg_polyline_last_vertex(HPOLYLINE polyline);
HVERTEX dwg_polyline_next_vertex(HVERTEX vertex);
HVERTEX dwg_polyline_prev_vertex(HVERTEX vertex);

long dwg_polyline_set_closed(HPOLYLINE polyline, long closed);
long dwg_polyline_is_closed(HPOLYLINE polyline);

void dwg_polyline_set_elevation(HPOLYLINE polyline, double elevation);
double dwg_polyline_get_elevation(HPOLYLINE polyline);

#ifdef __cplusplus
}
#endif

#endif
