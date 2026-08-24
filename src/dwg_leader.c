#include <stdlib.h>
#include <string.h>

#include "dwg_leader.h"
#include "dwg_document.h"

static void dwg_leader_copy_text(char *dst,
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

static DWG_LEADER *dwg_leader_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_LEADER)
        return NULL;

    return (DWG_LEADER *)hEntity->geometry;
}

HENTITY dwg_add_leader(HDWG hDwg)
{
    DWG_LEADER *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_LEADER);
    if (entity == NULL)
        return NULL;

    data = (DWG_LEADER *)malloc(sizeof(DWG_LEADER));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    memset(data, 0, sizeof(DWG_LEADER));

    data->arrow_size = 1.0;
    data->text_height = 2.5;

    entity->geometry = data;

    return entity;
}

HVERTEX dwg_leader_add_vertex(HENTITY hEntity, double x, double y, double z)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);
    HVERTEX vertex;

    if (data == NULL)
        return NULL;

    vertex = dwg_vertex_create(x, y, z);
    if (vertex == NULL)
        return NULL;

    if (data->vertex_first == NULL)
    {
        data->vertex_first = vertex;
        data->vertex_last = vertex;
    }
    else
    {
        vertex->prev = data->vertex_last;

        data->vertex_last->next = vertex;

        data->vertex_last = vertex;
    }

    data->vertex_count++;

    return vertex;
}

HVERTEX dwg_leader_first_vertex(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->vertex_first;
}

HVERTEX dwg_leader_next_vertex(HVERTEX vertex)
{
    if (vertex == NULL)
        return NULL;

    return vertex->next;
}

unsigned long dwg_leader_vertex_count(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return 0UL;

    return data->vertex_count;
}

double dwg_leader_get_arrow_size(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    return data != NULL ? data->arrow_size : 0.0;
}

long dwg_leader_set_arrow_size(HENTITY hEntity, double size)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->arrow_size = size;

    return 1L;
}

DWG_BOOL dwg_leader_get_spline(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    return data != NULL ? data->spline : DWG_FALSE;
}

long dwg_leader_set_spline(HENTITY hEntity, DWG_BOOL spline)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->spline = spline;

    return 1L;
}

const char *dwg_leader_get_text(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->text;
}

long dwg_leader_set_text(HENTITY hEntity, const char *text)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_leader_copy_text(data->text, DWG_LEADER_TEXT_MAX, text);

    return 1L;
}

double dwg_leader_get_text_height(HENTITY hEntity)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    return data != NULL ? data->text_height : 0.0;
}

long dwg_leader_set_text_height(HENTITY hEntity, double height)
{
    DWG_LEADER *data = dwg_leader_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->text_height = height;

    return 1L;
}
