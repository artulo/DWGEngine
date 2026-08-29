#include <stdlib.h>
#include <string.h>
#include "dwg_entity.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_hatch.h"
#include "dwg_leader.h"

static void dwg_entity_copy_text(char *dst,
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

HENTITY dwg_entity_create(HDWG hDwg, DWG_ENTITY_TYPE type)
{
    DWG_ENTITY *entity;
    (void)hDwg;

    entity = (DWG_ENTITY *)malloc(sizeof(DWG_ENTITY));
    if (entity == NULL)
        return NULL;

    memset(entity, 0, sizeof(DWG_ENTITY));

    entity->type = type;
    entity->color = 256;
    entity->layer[0] = '\0';
    entity->linetype[0] = '\0';

    return entity;
}

static void dwg_entity_free_vertex_list(HVERTEX first)
{
    HVERTEX vertex;
    HVERTEX next_vertex;

    vertex = first;

    while (vertex != NULL)
    {
        next_vertex = vertex->next;

        dwg_vertex_destroy(vertex);

        vertex = next_vertex;
    }
}

void dwg_entity_destroy(HENTITY hEntity)
{
    if (hEntity == NULL)
        return;

    if (hEntity->ex_data != NULL)
        free(hEntity->ex_data);

    if (hEntity->geometry != NULL)
    {
        /* Some entity types own a nested list of malloc'd nodes inside
           their geometry struct (vertices for POLYLINE, boundary points
           for HATCH) -- those need freeing individually before the
           geometry struct itself, or they leak. */
        switch (hEntity->type)
        {
        case DWG_ENTITY_POLYLINE:
            dwg_entity_free_vertex_list(((DWG_POLYLINE *)hEntity->geometry)->first_vertex);
            break;

        case DWG_ENTITY_HATCH:
            dwg_entity_free_vertex_list(((DWG_HATCH *)hEntity->geometry)->boundary_first);
            break;

        case DWG_ENTITY_LEADER:
            dwg_entity_free_vertex_list(((DWG_LEADER *)hEntity->geometry)->vertex_first);
            break;

        default:
            break;
        }

        free(hEntity->geometry);
    }

    free(hEntity);
}

DWG_ID dwg_entity_get_id(HENTITY hEntity)
{
    return hEntity != NULL ? hEntity->id : 0UL;
}

DWG_ENTITY_TYPE dwg_entity_get_type(HENTITY hEntity)
{
    return hEntity != NULL ? hEntity->type : DWG_ENTITY_UNKNOWN;
}

long dwg_entity_put_color(HENTITY hEntity, unsigned short color)
{
    if (hEntity == NULL)
        return 0L;
    hEntity->color = color;
    return 1L;
}

unsigned short dwg_entity_get_color(HENTITY hEntity)
{
    return hEntity != NULL ? hEntity->color : 0;
}

long dwg_entity_put_layer(HENTITY hEntity, const char *name)
{
    if (hEntity == NULL)
        return 0L;

    dwg_entity_copy_text(hEntity->layer, sizeof(hEntity->layer), name);

    return 1L;
}

const char *dwg_entity_get_layer(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    return hEntity->layer;
}

long dwg_entity_put_linetype(HENTITY hEntity, const char *name)
{
    if (hEntity == NULL)
        return 0L;

    dwg_entity_copy_text(hEntity->linetype, sizeof(hEntity->linetype), name);

    return 1L;
}

const char *dwg_entity_get_linetype(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    return hEntity->linetype;
}

long dwg_entity_put_user_data(HENTITY hEntity, void *user_data)
{
    if (hEntity == NULL)
        return 0L;
    hEntity->user_data = user_data;
    return 1L;
}

void *dwg_entity_get_user_data(HENTITY hEntity)
{
    return hEntity != NULL ? hEntity->user_data : NULL;
}

long dwg_entity_put_ex_data(HENTITY hEntity,
                            const void *data,
                            unsigned long size)
{
    unsigned char *buffer;

    if (hEntity == NULL)
        return 0L;

    if (size == 0UL)
    {
        if (hEntity->ex_data != NULL)
            free(hEntity->ex_data);
        hEntity->ex_data = NULL;
        hEntity->ex_data_size = 0UL;
        return 1L;
    }

    if (data == NULL)
        return 0L;

    buffer = (unsigned char *)malloc(size);
    if (buffer == NULL)
        return 0L;

    memcpy(buffer, data, size);

    if (hEntity->ex_data != NULL)
        free(hEntity->ex_data);

    hEntity->ex_data = buffer;
    hEntity->ex_data_size = size;

    return 1L;
}

const void *dwg_entity_get_ex_data(HENTITY hEntity,
                                   unsigned long *size)
{
    if (size != NULL)
        *size = 0UL;

    if (hEntity == NULL)
        return NULL;

    if (size != NULL)
        *size = hEntity->ex_data_size;

    return hEntity->ex_data;
}
