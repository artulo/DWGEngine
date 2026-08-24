#include <stdlib.h>
#include <string.h>

#include "dwg_layer.h"

static void dwg_layer_copy_text(char *dst,
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

HLAYER dwg_layer_create(const char *name)
{
    HLAYER layer;

    layer = (HLAYER)malloc(sizeof(DWG_LAYER));

    if (layer == NULL)
        return NULL;

    memset(layer, 0, sizeof(DWG_LAYER));

    dwg_layer_copy_text(layer->name,
                        DWG_LAYER_NAME_MAX,
                        name);

    layer->color = 7;
    layer->flags = DWG_LAYER_PLOTTABLE;

    layer->linetype[0] = '\0';

    layer->next = NULL;
    layer->prev = NULL;

    return layer;
}

void dwg_layer_destroy(HLAYER layer)
{
    if (layer == NULL)
        return;

    free(layer);
}

const char *dwg_layer_get_name(HLAYER layer)
{
    if (layer == NULL)
        return NULL;

    return layer->name;
}

long dwg_layer_set_name(HLAYER layer,
                        const char *name)
{
    if (layer == NULL)
        return 0L;

    dwg_layer_copy_text(layer->name,
                        DWG_LAYER_NAME_MAX,
                        name);

    return 1L;
}

unsigned short dwg_layer_get_color(HLAYER layer)
{
    if (layer == NULL)
        return 0;

    return layer->color;
}

long dwg_layer_set_color(HLAYER layer,
                         unsigned short color)
{
    if (layer == NULL)
        return 0L;

    layer->color = color;

    return 1L;
}

const char *dwg_layer_get_linetype(HLAYER layer)
{
    if (layer == NULL)
        return NULL;

    return layer->linetype;
}

long dwg_layer_set_linetype(HLAYER layer,
                            const char *name)
{
    if (layer == NULL)
        return 0L;

    dwg_layer_copy_text(layer->linetype,
                        64UL,
                        name);

    return 1L;
}

unsigned short dwg_layer_get_flags(HLAYER layer)
{
    if (layer == NULL)
        return 0;

    return layer->flags;
}

long dwg_layer_set_flags(HLAYER layer,
                         unsigned short flags)
{
    if (layer == NULL)
        return 0L;

    layer->flags = flags;

    return 1L;
}

long dwg_layer_is_off(HLAYER layer)
{
    if (layer == NULL)
        return 0L;

    return (layer->flags & DWG_LAYER_OFF) != 0;
}

long dwg_layer_is_frozen(HLAYER layer)
{
    if (layer == NULL)
        return 0L;

    return (layer->flags & DWG_LAYER_FROZEN) != 0;
}

long dwg_layer_is_locked(HLAYER layer)
{
    if (layer == NULL)
        return 0L;

    return (layer->flags & DWG_LAYER_LOCKED) != 0;
}

long dwg_layer_is_plottable(HLAYER layer)
{
    if (layer == NULL)
        return 0L;

    return (layer->flags & DWG_LAYER_PLOTTABLE) != 0;
}

long dwg_layer_set_off(HLAYER layer,
                       long value)
{
    if (layer == NULL)
        return 0L;

    if (value)
        layer->flags |= DWG_LAYER_OFF;
    else
        layer->flags &= (unsigned short)~DWG_LAYER_OFF;

    return 1L;
}

long dwg_layer_set_frozen(HLAYER layer,
                          long value)
{
    if (layer == NULL)
        return 0L;

    if (value)
        layer->flags |= DWG_LAYER_FROZEN;
    else
        layer->flags &= (unsigned short)~DWG_LAYER_FROZEN;

    return 1L;
}

long dwg_layer_set_locked(HLAYER layer,
                          long value)
{
    if (layer == NULL)
        return 0L;

    if (value)
        layer->flags |= DWG_LAYER_LOCKED;
    else
        layer->flags &= (unsigned short)~DWG_LAYER_LOCKED;

    return 1L;
}

long dwg_layer_set_plottable(HLAYER layer,
                             long value)
{
    if (layer == NULL)
        return 0L;

    if (value)
        layer->flags |= DWG_LAYER_PLOTTABLE;
    else
        layer->flags &= (unsigned short)~DWG_LAYER_PLOTTABLE;

    return 1L;
}