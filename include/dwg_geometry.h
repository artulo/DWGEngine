#ifndef DWG_GEOMETRY_H
#define DWG_GEOMETRY_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

HENTITY dwg_add_point(HDWG hDwg, double x, double y, double z);
HENTITY dwg_add_line(HDWG hDwg,
                     double x1, double y1, double z1,
                     double x2, double y2, double z2);

HENTITY dwg_add_circle(HDWG hDwg,
                       double x, double y, double z,
                       double radius);

HENTITY dwg_add_arc(HDWG hDwg,
                    double cx, double cy, double cz,
                    double radius,
                    double start_angle,
                    double end_angle);

HENTITY dwg_add_ellipse(HDWG hDwg,
                        double cx, double cy, double cz,
                        double major_axis_x, double major_axis_y, double major_axis_z,
                        double axis_ratio,
                        double start_param,
                        double end_param);

HENTITY dwg_add_polyline(HDWG hDwg);
HVERTEX dwg_add_vertex(HENTITY hPolyline,
                       double x, double y, double z);
HVERTEX dwg_add_vertex2(HENTITY hPolyline,
                        double x, double y, double z,
                        double bulge,
                        double start_width,
                        double end_width);

#ifdef __cplusplus
}
#endif

#endif
