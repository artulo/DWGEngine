#include <stdlib.h>
#include <string.h>

#include "dwg_block.h"

static void dwg_block_copy_text(char *dst,
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

HBLOCK dwg_block_create(const char *name)
{
    HBLOCK block;

    block = (HBLOCK)malloc(sizeof(DWG_BLOCK));

    if (block == NULL)
        return NULL;

    memset(block, 0, sizeof(DWG_BLOCK));

    dwg_block_copy_text(block->name, DWG_BLOCK_NAME_MAX, name);

    block->next_entity_id = 1UL;

    return block;
}

void dwg_block_destroy(HBLOCK block)
{
    DWG_ENTITY *entity;
    DWG_ENTITY *next_entity;

    if (block == NULL)
        return;

    entity = block->entity_first;

    while (entity != NULL)
    {
        next_entity = entity->next;

        dwg_entity_destroy(entity);

        entity = next_entity;
    }

    free(block);
}

const char *dwg_block_get_name(HBLOCK block)
{
    if (block == NULL)
        return NULL;

    return block->name;
}

long dwg_block_set_name(HBLOCK block, const char *name)
{
    if (block == NULL)
        return 0L;

    dwg_block_copy_text(block->name, DWG_BLOCK_NAME_MAX, name);

    return 1L;
}

void dwg_block_get_base(HBLOCK block, double *x, double *y, double *z)
{
    if (block == NULL)
        return;

    if (x != NULL) *x = block->base.x;
    if (y != NULL) *y = block->base.y;
    if (z != NULL) *z = block->base.z;
}

void dwg_block_set_base(HBLOCK block, double x, double y, double z)
{
    if (block == NULL)
        return;

    block->base.x = x;
    block->base.y = y;
    block->base.z = z;
}

HENTITY dwg_block_add_entity(HBLOCK block, DWG_ENTITY_TYPE type)
{
    HENTITY entity;

    if (block == NULL)
        return NULL;

    entity = dwg_entity_create(NULL, type);

    if (entity == NULL)
        return NULL;

    entity->id = block->next_entity_id++;

    if (block->entity_first == NULL)
    {
        block->entity_first = entity;
        block->entity_last = entity;
    }
    else
    {
        entity->prev = block->entity_last;

        block->entity_last->next = entity;

        block->entity_last = entity;
    }

    block->entity_count++;

    return entity;
}

long dwg_block_attach_entity(HBLOCK block, HENTITY entity)
{
    if (block == NULL || entity == NULL)
        return 0L;

    if (block->entity_first == NULL)
    {
        block->entity_first = entity;
        block->entity_last = entity;
    }
    else
    {
        entity->prev = block->entity_last;

        block->entity_last->next = entity;

        block->entity_last = entity;
    }

    block->entity_count++;

    return 1L;
}

long dwg_block_remove_entity(HBLOCK block, HENTITY entity)
{
    if (block == NULL || entity == NULL)
        return 0L;

    if (entity->prev != NULL)
        entity->prev->next = entity->next;
    else
        block->entity_first = entity->next;

    if (entity->next != NULL)
        entity->next->prev = entity->prev;
    else
        block->entity_last = entity->prev;

    if (block->entity_count > 0UL)
        block->entity_count--;

    entity->next = NULL;
    entity->prev = NULL;

    dwg_entity_destroy(entity);

    return 1L;
}

HENTITY dwg_block_first_entity(HBLOCK block)
{
    if (block == NULL)
        return NULL;

    return block->entity_first;
}

HENTITY dwg_block_next_entity(HENTITY entity)
{
    if (entity == NULL)
        return NULL;

    return entity->next;
}

unsigned long dwg_block_entity_count(HBLOCK block)
{
    if (block == NULL)
        return 0UL;

    return block->entity_count;
}

DWG_BOOL dwg_block_is_xref(HBLOCK block)
{
    if (block == NULL)
        return DWG_FALSE;

    return block->is_xref;
}

const char *dwg_block_get_xref_path(HBLOCK block)
{
    if (block == NULL)
        return NULL;

    return block->xref_path;
}

long dwg_block_set_xref_path(HBLOCK block, const char *path)
{
    if (block == NULL)
        return 0L;

    dwg_block_copy_text(block->xref_path, DWG_BLOCK_PATH_MAX, path);

    block->is_xref = (path != NULL && path[0] != '\0') ? DWG_TRUE : DWG_FALSE;

    return 1L;
}
