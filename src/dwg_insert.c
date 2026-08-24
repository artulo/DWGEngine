#include <stdlib.h>
#include <string.h>

#include "dwg_insert.h"
#include "dwg_document.h"

static void dwg_insert_copy_text(char *dst,
                                 unsigned long size,
                                 const char *src)
{
    unsigned long i;

    if (dst == NULL || size == 0UL)
        return;

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    for (i = 0UL; i + 1UL < size && src[i] != '\0'; ++i)
        dst[i] = src[i];

    dst[i] = '\0';
}

static DWG_INSERT *dwg_insert_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_INSERT)
        return NULL;

    return (DWG_INSERT *)hEntity->geometry;
}

HENTITY dwg_add_insert(HDWG hDwg,
                       const char *block_name,
                       double x, double y, double z,
                       double angle)
{
    DWG_INSERT *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_INSERT);
    if (entity == NULL)
        return NULL;

    data = (DWG_INSERT *)malloc(sizeof(DWG_INSERT));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    memset(data, 0, sizeof(DWG_INSERT));

    dwg_insert_copy_text(data->block_name, DWG_BLOCK_NAME_MAX, block_name);

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;

    data->scale_x = 1.0;
    data->scale_y = 1.0;
    data->scale_z = 1.0;

    data->angle = angle;

    entity->geometry = data;

    return entity;
}

const char *dwg_insert_get_block_name(HENTITY hEntity)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->block_name;
}

long dwg_insert_set_block_name(HENTITY hEntity, const char *block_name)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_insert_copy_text(data->block_name, DWG_BLOCK_NAME_MAX, block_name);

    return 1L;
}

void dwg_insert_get_point(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->point.x;
    if (y != NULL) *y = data->point.y;
    if (z != NULL) *z = data->point.z;
}

void dwg_insert_set_point(HENTITY hEntity, double x, double y, double z)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return;

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;
}

void dwg_insert_get_scale(HENTITY hEntity, double *sx, double *sy, double *sz)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return;

    if (sx != NULL) *sx = data->scale_x;
    if (sy != NULL) *sy = data->scale_y;
    if (sz != NULL) *sz = data->scale_z;
}

void dwg_insert_set_scale(HENTITY hEntity, double sx, double sy, double sz)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return;

    data->scale_x = sx;
    data->scale_y = sy;
    data->scale_z = sz;
}

double dwg_insert_get_angle(HENTITY hEntity)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    return data != NULL ? data->angle : 0.0;
}

long dwg_insert_set_angle(HENTITY hEntity, double angle)
{
    DWG_INSERT *data = dwg_insert_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->angle = angle;

    return 1L;
}
