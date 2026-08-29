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

HENTITY dwg_add_polyline(HDWG hDwg);
HVERTEX dwg_add_vertex(HENTITY hPolyline,
                       double x, double y, double z);
HVERTEX dwg_add_vertex2(HENTITY hPolyline,
                        double x, double y, double z,
                        double bulge,
                        double start_width,
                        double end_width);

/*
 * Getters/setters de geometria para POINT/LINE/CIRCLE/ARC -- dwg_add_*
 * de arriba solo escribe al crear, nunca hubo forma de LEER o EDITAR
 * estos campos despues (a diferencia de TEXT/MTEXT/SOLID/HATCH/
 * POLYLINE, que ya tenian su propio get/set desde el reverse original).
 * Agregados para el panel de propiedades del visor (seleccion +
 * edicion interactiva, pedido de Arturo 2026-08-26) -- mismo estilo
 * exacto que dwg_text_get/set_point: castear hEntity->geometry al
 * struct real (DWG_POINT3D/DWG_LINE3D/DWG_CIRCLE3D/DWG_ARC3D, ya en
 * dwg_types.h) y listo, sin logica nueva.
 */
void dwg_point_get_xyz(HENTITY hEntity, double *x, double *y, double *z);
long dwg_point_set_xyz(HENTITY hEntity, double x, double y, double z);

void dwg_line_get_start(HENTITY hEntity, double *x, double *y, double *z);
long dwg_line_set_start(HENTITY hEntity, double x, double y, double z);
void dwg_line_get_end(HENTITY hEntity, double *x, double *y, double *z);
long dwg_line_set_end(HENTITY hEntity, double x, double y, double z);

void dwg_circle_get_center(HENTITY hEntity, double *x, double *y, double *z);
long dwg_circle_set_center(HENTITY hEntity, double x, double y, double z);
double dwg_circle_get_radius(HENTITY hEntity);
long dwg_circle_set_radius(HENTITY hEntity, double radius);

void dwg_arc_get_center(HENTITY hEntity, double *x, double *y, double *z);
long dwg_arc_set_center(HENTITY hEntity, double x, double y, double z);
double dwg_arc_get_radius(HENTITY hEntity);
long dwg_arc_set_radius(HENTITY hEntity, double radius);
void dwg_arc_get_angles(HENTITY hEntity, double *start_angle, double *end_angle);
long dwg_arc_set_angles(HENTITY hEntity, double start_angle, double end_angle);

#ifdef __cplusplus
}
#endif

#endif
