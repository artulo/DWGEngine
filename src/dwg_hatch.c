#include <stdlib.h>
#include <string.h>

#include "dwg_hatch.h"
#include "dwg_document.h"

static void dwg_hatch_copy_text(char *dst,
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

static DWG_HATCH *dwg_hatch_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_HATCH)
        return NULL;

    return (DWG_HATCH *)hEntity->geometry;
}

HENTITY dwg_add_hatch(HDWG hDwg,
                      const char *pattern_name,
                      double angle,
                      double scale,
                      DWG_BOOL solid)
{
    DWG_HATCH *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_HATCH);
    if (entity == NULL)
        return NULL;

    data = (DWG_HATCH *)malloc(sizeof(DWG_HATCH));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    memset(data, 0, sizeof(DWG_HATCH));

    dwg_hatch_copy_text(data->pattern_name, DWG_HATCH_PATTERN_MAX, pattern_name);

    data->angle = angle;
    data->scale = scale;
    data->solid = solid;

    entity->geometry = data;

    return entity;
}

const char *dwg_hatch_get_pattern(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->pattern_name;
}

long dwg_hatch_set_pattern(HENTITY hEntity, const char *pattern_name)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_hatch_copy_text(data->pattern_name, DWG_HATCH_PATTERN_MAX, pattern_name);

    return 1L;
}

double dwg_hatch_get_angle(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    return data != NULL ? data->angle : 0.0;
}

long dwg_hatch_set_angle(HENTITY hEntity, double angle)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->angle = angle;

    return 1L;
}

double dwg_hatch_get_scale(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    return data != NULL ? data->scale : 0.0;
}

long dwg_hatch_set_scale(HENTITY hEntity, double scale)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->scale = scale;

    return 1L;
}

DWG_BOOL dwg_hatch_get_solid(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    return data != NULL ? data->solid : DWG_FALSE;
}

long dwg_hatch_set_solid(HENTITY hEntity, DWG_BOOL solid)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->solid = solid;

    return 1L;
}

HVERTEX dwg_hatch_add_boundary_point(HENTITY hEntity, double x, double y, double z)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);
    HVERTEX vertex;

    if (data == NULL)
        return NULL;

    vertex = dwg_vertex_create(x, y, z);
    if (vertex == NULL)
        return NULL;

    if (data->boundary_first == NULL)
    {
        data->boundary_first = vertex;
        data->boundary_last = vertex;
    }
    else
    {
        vertex->prev = data->boundary_last;

        data->boundary_last->next = vertex;

        data->boundary_last = vertex;
    }

    data->boundary_count++;

    return vertex;
}

HVERTEX dwg_hatch_first_boundary_point(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->boundary_first;
}

HVERTEX dwg_hatch_next_boundary_point(HVERTEX vertex)
{
    if (vertex == NULL)
        return NULL;

    return vertex->next;
}

unsigned long dwg_hatch_boundary_count(HENTITY hEntity)
{
    DWG_HATCH *data = dwg_hatch_from_entity(hEntity);

    if (data == NULL)
        return 0UL;

    return data->boundary_count;
}
