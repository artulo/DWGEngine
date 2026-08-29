#include <stdlib.h>
#include <math.h>

#include "dwg_dimension.h"
#include "dwg_document.h"

HENTITY dwg_add_dimension_linear(HDWG hDwg,
                                 double x1, double y1, double z1,
                                 double x2, double y2, double z2,
                                 double defx, double defy, double defz)
{
    DWG_DIMENSION3D *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_DIMENSION);
    if (entity == NULL)
        return NULL;

    data = (DWG_DIMENSION3D *)malloc(sizeof(DWG_DIMENSION3D));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    data->xline1.x = x1; data->xline1.y = y1; data->xline1.z = z1;
    data->xline2.x = x2; data->xline2.y = y2; data->xline2.z = z2;
    data->def_pt.x = defx; data->def_pt.y = defy; data->def_pt.z = defz;

    entity->geometry = data;

    return entity;
}

static DWG_DIMENSION3D *dwg_dimension_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_DIMENSION)
        return NULL;

    return (DWG_DIMENSION3D *)hEntity->geometry;
}

void dwg_dimension_get_xline1(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->xline1.x;
    if (y != NULL) *y = data->xline1.y;
    if (z != NULL) *z = data->xline1.z;
}

long dwg_dimension_set_xline1(HENTITY hEntity, double x, double y, double z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->xline1.x = x;
    data->xline1.y = y;
    data->xline1.z = z;

    return 1L;
}

void dwg_dimension_get_xline2(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->xline2.x;
    if (y != NULL) *y = data->xline2.y;
    if (z != NULL) *z = data->xline2.z;
}

long dwg_dimension_set_xline2(HENTITY hEntity, double x, double y, double z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->xline2.x = x;
    data->xline2.y = y;
    data->xline2.z = z;

    return 1L;
}

void dwg_dimension_get_def_pt(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->def_pt.x;
    if (y != NULL) *y = data->def_pt.y;
    if (z != NULL) *z = data->def_pt.z;
}

long dwg_dimension_set_def_pt(HENTITY hEntity, double x, double y, double z)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->def_pt.x = x;
    data->def_pt.y = y;
    data->def_pt.z = z;

    return 1L;
}

double dwg_dimension_get_value(HENTITY hEntity)
{
    DWG_DIMENSION3D *data = dwg_dimension_from_entity(hEntity);
    double dx, dy;

    if (data == NULL)
        return 0.0;

    /* Solo XY -- toda la matematica de proyeccion perpendicular
       (bridge_dimension, draw_dimension) trabaja en el plano XY (Z
       viaja sin tocar), asi que el valor medido tiene que ser
       consistente con eso, no la distancia 3D real si xline1/xline2
       llegaran a diferir en Z. */
    dx = data->xline2.x - data->xline1.x;
    dy = data->xline2.y - data->xline1.y;

    return sqrt(dx * dx + dy * dy);
}
