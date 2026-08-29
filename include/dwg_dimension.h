#ifndef DWG_DIMENSION_H
#define DWG_DIMENSION_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cota lineal ALINEADA (equivalente a DIMENSION ALIGNED, no LINEAR con
 * angulo forzado horizontal/vertical -- pedido de Arturo 2026-08-26,
 * "permitir colocar cotas"; RADIUS/DIAMETER/ANGULAR quedan fuera de
 * alcance esta vuelta, ver bridge_dimension en dwg_libredwg_bridge.c
 * para el mismo alcance ya aceptado del lado lector).
 *
 * Deliberadamente NO guarda un angulo de rotacion aparte -- la
 * direccion de la linea de cota es SIEMPRE atan2(xline2-xline1),
 * recalculada donde haga falta (dwg_render.c/dwg_transform.c) en vez
 * de guardada. Con eso, mover/rotar/escalar/espejar esta entidad es
 * tan simple como para DWG_SOLID3D/DWG_FACE3D (mover los 3 puntos y
 * listo) -- evita sincronizar un angulo aparte en cada transform, la
 * misma clase de bug que un angulo de ARC ya exige cuidado especial al
 * espejar (ver el comentario de dwg_transform.h sobre eso).
 *
 * Las lineas de extension + la linea de cota se REGENERAN a partir de
 * estos 3 puntos via proyeccion perpendicular de def_pt contra la
 * direccion xline1->xline2 -- misma formula ya escrita y verificada
 * (Arturo la confirmo visualmente en su momento) en bridge_dimension()
 * de dwg_libredwg_bridge.c (lineas 1246-1263), reusada tal cual en
 * dwg_render.c's draw_dimension y en dwg_transform.c's explode.
 */
typedef struct _DWG_DIMENSION3D
{
    DWG_POINT3D xline1;   /* primer punto medido */
    DWG_POINT3D xline2;   /* segundo punto medido */
    DWG_POINT3D def_pt;   /* ubica la linea de cota -- offset perpendicular a xline1->xline2 */
} DWG_DIMENSION3D;

HENTITY dwg_add_dimension_linear(HDWG hDwg,
                                 double x1, double y1, double z1,
                                 double x2, double y2, double z2,
                                 double defx, double defy, double defz);

void dwg_dimension_get_xline1(HENTITY hEntity, double *x, double *y, double *z);
long dwg_dimension_set_xline1(HENTITY hEntity, double x, double y, double z);

void dwg_dimension_get_xline2(HENTITY hEntity, double *x, double *y, double *z);
long dwg_dimension_set_xline2(HENTITY hEntity, double x, double y, double z);

void dwg_dimension_get_def_pt(HENTITY hEntity, double *x, double *y, double *z);
long dwg_dimension_set_def_pt(HENTITY hEntity, double x, double y, double z);

/* Distancia(xline1,xline2) -- el valor medido, siempre derivado (nunca
   se guarda ni se puede desincronizar de la geometria real). */
double dwg_dimension_get_value(HENTITY hEntity);

#ifdef __cplusplus
}
#endif

#endif
