#include <stdlib.h>
#include <string.h>

#include "dwg_solid.h"
#include "dwg_document.h"

HENTITY dwg_add_solid(HDWG hDwg,
                      double x1, double y1, double z1,
                      double x2, double y2, double z2,
                      double x3, double y3, double z3,
                      double x4, double y4, double z4)
{
    DWG_SOLID3D *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_SOLID);
    if (entity == NULL)
        return NULL;

    data = (DWG_SOLID3D *)malloc(sizeof(DWG_SOLID3D));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    data->p1.x = x1; data->p1.y = y1; data->p1.z = z1;
    data->p2.x = x2; data->p2.y = y2; data->p2.z = z2;
    data->p3.x = x3; data->p3.y = y3; data->p3.z = z3;
    data->p4.x = x4; data->p4.y = y4; data->p4.z = z4;

    entity->geometry = data;

    return entity;
}

static DWG_FACE3D *dwg_face_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_FACE)
        return NULL;

    return (DWG_FACE3D *)hEntity->geometry;
}

HENTITY dwg_add_face(HDWG hDwg,
                     double x1, double y1, double z1,
                     double x2, double y2, double z2,
                     double x3, double y3, double z3,
                     double x4, double y4, double z4)
{
    DWG_FACE3D *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_FACE);
    if (entity == NULL)
        return NULL;

    data = (DWG_FACE3D *)malloc(sizeof(DWG_FACE3D));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    data->p1.x = x1; data->p1.y = y1; data->p1.z = z1;
    data->p2.x = x2; data->p2.y = y2; data->p2.z = z2;
    data->p3.x = x3; data->p3.y = y3; data->p3.z = z3;
    data->p4.x = x4; data->p4.y = y4; data->p4.z = z4;
    data->edge_flags = 0;

    entity->geometry = data;

    return entity;
}

unsigned short dwg_face_get_edge_flags(HENTITY hEntity)
{
    DWG_FACE3D *data = dwg_face_from_entity(hEntity);

    return data != NULL ? data->edge_flags : 0;
}

long dwg_face_set_edge_flags(HENTITY hEntity, unsigned short flags)
{
    DWG_FACE3D *data = dwg_face_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->edge_flags = flags;

    return 1L;
}
