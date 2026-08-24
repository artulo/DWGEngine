#include <stdlib.h>
#include <string.h>

#include "dwg_document.h"
#include "dwg_entity.h"

static int dwg_name_equal(const char *a,
                          const char *b)
{
    if (a == NULL || b == NULL)
        return 0;

    return strcmp(a, b) == 0;
}

/* Removes hEntity from the selection array if present (swap-with-last,
   order doesn't matter for a selection set) -- called when an entity is
   destroyed so the selection never holds a dangling pointer, regardless
   of whether it was erased via dwg_document_sel_erase or directly via
   dwg_document_remove_entity. */
static void dwg_document_sel_purge(HDWG hDwg, HENTITY hEntity)
{
    unsigned long i;

    if (hDwg == NULL || hEntity == NULL)
        return;

    for (i = 0UL; i < hDwg->sel_count; i++)
    {
        if (hDwg->sel_items[i] == hEntity)
        {
            hDwg->sel_items[i] = hDwg->sel_items[hDwg->sel_count - 1UL];
            hDwg->sel_count--;
            return;
        }
    }
}

HDWG dwg_document_create(void)
{
    HDWG hDwg;

    hDwg = (HDWG)malloc(sizeof(DWG_DOCUMENT));

    if (hDwg == NULL)
        return NULL;

    memset(hDwg, 0, sizeof(DWG_DOCUMENT));

    hDwg->next_entity_id = 1UL;

    return hDwg;
}

void dwg_document_destroy(HDWG hDwg)
{
    HENTITY entity;
    HENTITY next_entity;

    HLAYER layer;
    HLAYER next_layer;

    HPAGE page;
    HPAGE next_page;

    HBLOCK block;
    HBLOCK next_block;

    HLINETYPE linetype;
    HLINETYPE next_linetype;

    HSTYLE style;
    HSTYLE next_style;

    HPOINTSTYLE pointstyle;
    HPOINTSTYLE next_pointstyle;

    HMLINESTYLE mlinestyle;
    HMLINESTYLE next_mlinestyle;

    HDIMSTYLE dimstyle;
    HDIMSTYLE next_dimstyle;

    if (hDwg == NULL)
        return;

    entity = hDwg->entity_first;

    while (entity != NULL)
    {
        next_entity = entity->next;

        dwg_entity_destroy(entity);

        entity = next_entity;
    }

    layer = hDwg->layer_first;

    while (layer != NULL)
    {
        next_layer = layer->next;

        dwg_layer_destroy(layer);

        layer = next_layer;
    }

    page = hDwg->page_first;

    while (page != NULL)
    {
        next_page = page->next;

        dwg_page_destroy(page);

        page = next_page;
    }

    block = hDwg->block_first;

    while (block != NULL)
    {
        next_block = block->next;

        dwg_block_destroy(block);

        block = next_block;
    }

    linetype = hDwg->linetype_first;

    while (linetype != NULL)
    {
        next_linetype = linetype->next;

        dwg_linetype_destroy(linetype);

        linetype = next_linetype;
    }

    style = hDwg->style_first;

    while (style != NULL)
    {
        next_style = style->next;

        dwg_style_destroy(style);

        style = next_style;
    }

    pointstyle = hDwg->pointstyle_first;

    while (pointstyle != NULL)
    {
        next_pointstyle = pointstyle->next;

        dwg_pointstyle_destroy(pointstyle);

        pointstyle = next_pointstyle;
    }

    mlinestyle = hDwg->mlinestyle_first;

    while (mlinestyle != NULL)
    {
        next_mlinestyle = mlinestyle->next;

        dwg_mlinestyle_destroy(mlinestyle);

        mlinestyle = next_mlinestyle;
    }

    dimstyle = hDwg->dimstyle_first;

    while (dimstyle != NULL)
    {
        next_dimstyle = dimstyle->next;

        dwg_dimstyle_destroy(dimstyle);

        dimstyle = next_dimstyle;
    }

    if (hDwg->sel_items != NULL)
        free(hDwg->sel_items); /* borrowed pointers, entities already freed above */

    free(hDwg);
}

HENTITY dwg_document_add_entity(HDWG hDwg,
                                DWG_ENTITY_TYPE type)
{
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_entity_create(hDwg, type);

    if (entity == NULL)
        return NULL;

    entity->id = hDwg->next_entity_id++;

    if (hDwg->entity_first == NULL)
    {
        hDwg->entity_first = entity;
        hDwg->entity_last = entity;
    }
    else
    {
        entity->prev = hDwg->entity_last;

        hDwg->entity_last->next = entity;

        hDwg->entity_last = entity;
    }

    hDwg->entity_count++;

    return entity;
}

long dwg_document_remove_entity(HDWG hDwg,
                                HENTITY hEntity)
{
    if (hDwg == NULL || hEntity == NULL)
        return 0L;

    if (hEntity->prev != NULL)
        hEntity->prev->next = hEntity->next;
    else
        hDwg->entity_first = hEntity->next;

    if (hEntity->next != NULL)
        hEntity->next->prev = hEntity->prev;
    else
        hDwg->entity_last = hEntity->prev;

    if (hDwg->entity_count > 0UL)
        hDwg->entity_count--;

    hEntity->next = NULL;
    hEntity->prev = NULL;

    dwg_document_sel_purge(hDwg, hEntity);
    dwg_entity_destroy(hEntity);

    return 1L;
}

HENTITY dwg_document_detach_entity(HDWG hDwg,
                                   HENTITY hEntity)
{
    if (hDwg == NULL || hEntity == NULL)
        return NULL;

    if (hEntity->prev != NULL)
        hEntity->prev->next = hEntity->next;
    else
        hDwg->entity_first = hEntity->next;

    if (hEntity->next != NULL)
        hEntity->next->prev = hEntity->prev;
    else
        hDwg->entity_last = hEntity->prev;

    if (hDwg->entity_count > 0UL)
        hDwg->entity_count--;

    hEntity->next = NULL;
    hEntity->prev = NULL;

    dwg_document_sel_purge(hDwg, hEntity); /* no longer part of the main list this selection indexes into */

    return hEntity;
}

HENTITY dwg_document_get_entity_by_id(HDWG hDwg,
                                       DWG_ID id)
{
    HENTITY entity;

    if (hDwg == NULL || id == 0UL)
        return NULL;

    entity = hDwg->entity_first;

    while (entity != NULL)
    {
        if (entity->id == id)
            return entity;

        entity = entity->next;
    }

    return NULL;
}

HENTITY dwg_document_first_entity(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->entity_first;
}

HENTITY dwg_document_next_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    return hEntity->next;
}

unsigned long dwg_document_entity_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->entity_count;
}

/* ----------------------------------------------------------
   LAYERS
   ---------------------------------------------------------- */

HLAYER dwg_document_add_layer(HDWG hDwg,
                              const char *name)
{
    HLAYER layer;

    if (hDwg == NULL)
        return NULL;

    /*
     * AutoCAD no permite dos nombres de capa iguales.
     */
    if (dwg_document_get_layer(hDwg, name) != NULL)
        return NULL;

    layer = dwg_layer_create(name);

    if (layer == NULL)
        return NULL;

    if (hDwg->layer_first == NULL)
    {
        hDwg->layer_first = layer;
        hDwg->layer_last = layer;
    }
    else
    {
        layer->prev = hDwg->layer_last;

        hDwg->layer_last->next = layer;

        hDwg->layer_last = layer;
    }

    hDwg->layer_count++;

    return layer;
}

long dwg_document_remove_layer(HDWG hDwg,
                               HLAYER layer)
{
    if (hDwg == NULL || layer == NULL)
        return 0L;

    if (layer->prev != NULL)
        layer->prev->next = layer->next;
    else
        hDwg->layer_first = layer->next;

    if (layer->next != NULL)
        layer->next->prev = layer->prev;
    else
        hDwg->layer_last = layer->prev;

    if (hDwg->layer_count > 0UL)
        hDwg->layer_count--;

    layer->next = NULL;
    layer->prev = NULL;

    dwg_layer_destroy(layer);

    return 1L;
}

HLAYER dwg_document_get_layer(HDWG hDwg,
                              const char *name)
{
    HLAYER layer;

    if (hDwg == NULL || name == NULL)
        return NULL;

    layer = hDwg->layer_first;

    while (layer != NULL)
    {
        if (dwg_name_equal(layer->name, name))
            return layer;

        layer = layer->next;
    }

    return NULL;
}

HLAYER dwg_document_first_layer(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->layer_first;
}

HLAYER dwg_document_next_layer(HLAYER layer)
{
    if (layer == NULL)
        return NULL;

    return layer->next;
}

unsigned long dwg_document_layer_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->layer_count;
}

/* ----------------------------------------------------------
   PAGES
   ---------------------------------------------------------- */

HPAGE dwg_document_add_page(HDWG hDwg,
                            const char *name)
{
    HPAGE page;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_page(hDwg, name) != NULL)
        return NULL;

    page = dwg_page_create(name);

    if (page == NULL)
        return NULL;

    if (hDwg->page_first == NULL)
    {
        hDwg->page_first = page;
        hDwg->page_last = page;
    }
    else
    {
        page->prev = hDwg->page_last;

        hDwg->page_last->next = page;

        hDwg->page_last = page;
    }

    hDwg->page_count++;

    return page;
}

long dwg_document_remove_page(HDWG hDwg,
                              HPAGE page)
{
    if (hDwg == NULL || page == NULL)
        return 0L;

    if (page->prev != NULL)
        page->prev->next = page->next;
    else
        hDwg->page_first = page->next;

    if (page->next != NULL)
        page->next->prev = page->prev;
    else
        hDwg->page_last = page->prev;

    if (hDwg->page_count > 0UL)
        hDwg->page_count--;

    page->next = NULL;
    page->prev = NULL;

    dwg_page_destroy(page);

    return 1L;
}

HPAGE dwg_document_get_page(HDWG hDwg,
                            const char *name)
{
    HPAGE page;

    if (hDwg == NULL || name == NULL)
        return NULL;

    page = hDwg->page_first;

    while (page != NULL)
    {
        if (dwg_name_equal(dwg_page_get_name(page), name))
            return page;

        page = page->next;
    }

    return NULL;
}

HPAGE dwg_document_first_page(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->page_first;
}

HPAGE dwg_document_next_page(HPAGE page)
{
    if (page == NULL)
        return NULL;

    return page->next;
}

unsigned long dwg_document_page_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->page_count;
}

/* ----------------------------------------------------------
   BLOCKS
   ---------------------------------------------------------- */

HBLOCK dwg_document_add_block(HDWG hDwg,
                              const char *name)
{
    HBLOCK block;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_block(hDwg, name) != NULL)
        return NULL;

    block = dwg_block_create(name);

    if (block == NULL)
        return NULL;

    if (hDwg->block_first == NULL)
    {
        hDwg->block_first = block;
        hDwg->block_last = block;
    }
    else
    {
        block->prev = hDwg->block_last;

        hDwg->block_last->next = block;

        hDwg->block_last = block;
    }

    hDwg->block_count++;

    return block;
}

long dwg_document_remove_block(HDWG hDwg,
                               HBLOCK block)
{
    if (hDwg == NULL || block == NULL)
        return 0L;

    if (block->prev != NULL)
        block->prev->next = block->next;
    else
        hDwg->block_first = block->next;

    if (block->next != NULL)
        block->next->prev = block->prev;
    else
        hDwg->block_last = block->prev;

    if (hDwg->block_count > 0UL)
        hDwg->block_count--;

    block->next = NULL;
    block->prev = NULL;

    dwg_block_destroy(block);

    return 1L;
}

HBLOCK dwg_document_get_block(HDWG hDwg,
                              const char *name)
{
    HBLOCK block;

    if (hDwg == NULL || name == NULL)
        return NULL;

    block = hDwg->block_first;

    while (block != NULL)
    {
        if (dwg_name_equal(dwg_block_get_name(block), name))
            return block;

        block = block->next;
    }

    return NULL;
}

HBLOCK dwg_document_first_block(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->block_first;
}

HBLOCK dwg_document_next_block(HBLOCK block)
{
    if (block == NULL)
        return NULL;

    return block->next;
}

unsigned long dwg_document_block_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->block_count;
}

/* ----------------------------------------------------------
   LINETYPES
   ---------------------------------------------------------- */

HLINETYPE dwg_document_add_linetype(HDWG hDwg,
                                    const char *name)
{
    HLINETYPE linetype;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_linetype(hDwg, name) != NULL)
        return NULL;

    linetype = dwg_linetype_create(name);

    if (linetype == NULL)
        return NULL;

    if (hDwg->linetype_first == NULL)
    {
        hDwg->linetype_first = linetype;
        hDwg->linetype_last = linetype;
    }
    else
    {
        linetype->prev = hDwg->linetype_last;

        hDwg->linetype_last->next = linetype;

        hDwg->linetype_last = linetype;
    }

    hDwg->linetype_count++;

    return linetype;
}

long dwg_document_remove_linetype(HDWG hDwg,
                                  HLINETYPE linetype)
{
    if (hDwg == NULL || linetype == NULL)
        return 0L;

    if (linetype->prev != NULL)
        linetype->prev->next = linetype->next;
    else
        hDwg->linetype_first = linetype->next;

    if (linetype->next != NULL)
        linetype->next->prev = linetype->prev;
    else
        hDwg->linetype_last = linetype->prev;

    if (hDwg->linetype_count > 0UL)
        hDwg->linetype_count--;

    linetype->next = NULL;
    linetype->prev = NULL;

    dwg_linetype_destroy(linetype);

    return 1L;
}

HLINETYPE dwg_document_get_linetype(HDWG hDwg,
                                    const char *name)
{
    HLINETYPE linetype;

    if (hDwg == NULL || name == NULL)
        return NULL;

    linetype = hDwg->linetype_first;

    while (linetype != NULL)
    {
        if (dwg_name_equal(dwg_linetype_get_name(linetype), name))
            return linetype;

        linetype = linetype->next;
    }

    return NULL;
}

HLINETYPE dwg_document_first_linetype(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->linetype_first;
}

HLINETYPE dwg_document_next_linetype(HLINETYPE linetype)
{
    if (linetype == NULL)
        return NULL;

    return linetype->next;
}

unsigned long dwg_document_linetype_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->linetype_count;
}

/* ----------------------------------------------------------
   TEXT STYLES
   ---------------------------------------------------------- */

HSTYLE dwg_document_add_style(HDWG hDwg,
                              const char *name)
{
    HSTYLE style;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_style(hDwg, name) != NULL)
        return NULL;

    style = dwg_style_create(name);

    if (style == NULL)
        return NULL;

    if (hDwg->style_first == NULL)
    {
        hDwg->style_first = style;
        hDwg->style_last = style;
    }
    else
    {
        style->prev = hDwg->style_last;

        hDwg->style_last->next = style;

        hDwg->style_last = style;
    }

    hDwg->style_count++;

    return style;
}

long dwg_document_remove_style(HDWG hDwg,
                               HSTYLE style)
{
    if (hDwg == NULL || style == NULL)
        return 0L;

    if (style->prev != NULL)
        style->prev->next = style->next;
    else
        hDwg->style_first = style->next;

    if (style->next != NULL)
        style->next->prev = style->prev;
    else
        hDwg->style_last = style->prev;

    if (hDwg->style_count > 0UL)
        hDwg->style_count--;

    style->next = NULL;
    style->prev = NULL;

    dwg_style_destroy(style);

    return 1L;
}

HSTYLE dwg_document_get_style(HDWG hDwg,
                              const char *name)
{
    HSTYLE style;

    if (hDwg == NULL || name == NULL)
        return NULL;

    style = hDwg->style_first;

    while (style != NULL)
    {
        if (dwg_name_equal(dwg_style_get_name(style), name))
            return style;

        style = style->next;
    }

    return NULL;
}

HSTYLE dwg_document_first_style(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->style_first;
}

HSTYLE dwg_document_next_style(HSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->next;
}

unsigned long dwg_document_style_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->style_count;
}

/* ----------------------------------------------------------
   POINT STYLES
   ---------------------------------------------------------- */

HPOINTSTYLE dwg_document_add_pointstyle(HDWG hDwg,
                                        const char *name)
{
    HPOINTSTYLE style;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_pointstyle(hDwg, name) != NULL)
        return NULL;

    style = dwg_pointstyle_create(name);

    if (style == NULL)
        return NULL;

    if (hDwg->pointstyle_first == NULL)
    {
        hDwg->pointstyle_first = style;
        hDwg->pointstyle_last = style;
    }
    else
    {
        style->prev = hDwg->pointstyle_last;

        hDwg->pointstyle_last->next = style;

        hDwg->pointstyle_last = style;
    }

    hDwg->pointstyle_count++;

    return style;
}

long dwg_document_remove_pointstyle(HDWG hDwg,
                                    HPOINTSTYLE style)
{
    if (hDwg == NULL || style == NULL)
        return 0L;

    if (style->prev != NULL)
        style->prev->next = style->next;
    else
        hDwg->pointstyle_first = style->next;

    if (style->next != NULL)
        style->next->prev = style->prev;
    else
        hDwg->pointstyle_last = style->prev;

    if (hDwg->pointstyle_count > 0UL)
        hDwg->pointstyle_count--;

    style->next = NULL;
    style->prev = NULL;

    dwg_pointstyle_destroy(style);

    return 1L;
}

HPOINTSTYLE dwg_document_get_pointstyle(HDWG hDwg,
                                        const char *name)
{
    HPOINTSTYLE style;

    if (hDwg == NULL || name == NULL)
        return NULL;

    style = hDwg->pointstyle_first;

    while (style != NULL)
    {
        if (dwg_name_equal(dwg_pointstyle_get_name(style), name))
            return style;

        style = style->next;
    }

    return NULL;
}

HPOINTSTYLE dwg_document_first_pointstyle(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->pointstyle_first;
}

HPOINTSTYLE dwg_document_next_pointstyle(HPOINTSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->next;
}

unsigned long dwg_document_pointstyle_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->pointstyle_count;
}

/* ----------------------------------------------------------
   MLINE STYLES
   ---------------------------------------------------------- */

HMLINESTYLE dwg_document_add_mlinestyle(HDWG hDwg,
                                        const char *name)
{
    HMLINESTYLE style;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_mlinestyle(hDwg, name) != NULL)
        return NULL;

    style = dwg_mlinestyle_create(name);

    if (style == NULL)
        return NULL;

    if (hDwg->mlinestyle_first == NULL)
    {
        hDwg->mlinestyle_first = style;
        hDwg->mlinestyle_last = style;
    }
    else
    {
        style->prev = hDwg->mlinestyle_last;

        hDwg->mlinestyle_last->next = style;

        hDwg->mlinestyle_last = style;
    }

    hDwg->mlinestyle_count++;

    return style;
}

long dwg_document_remove_mlinestyle(HDWG hDwg,
                                    HMLINESTYLE style)
{
    if (hDwg == NULL || style == NULL)
        return 0L;

    if (style->prev != NULL)
        style->prev->next = style->next;
    else
        hDwg->mlinestyle_first = style->next;

    if (style->next != NULL)
        style->next->prev = style->prev;
    else
        hDwg->mlinestyle_last = style->prev;

    if (hDwg->mlinestyle_count > 0UL)
        hDwg->mlinestyle_count--;

    style->next = NULL;
    style->prev = NULL;

    dwg_mlinestyle_destroy(style);

    return 1L;
}

HMLINESTYLE dwg_document_get_mlinestyle(HDWG hDwg,
                                        const char *name)
{
    HMLINESTYLE style;

    if (hDwg == NULL || name == NULL)
        return NULL;

    style = hDwg->mlinestyle_first;

    while (style != NULL)
    {
        if (dwg_name_equal(dwg_mlinestyle_get_name(style), name))
            return style;

        style = style->next;
    }

    return NULL;
}

HMLINESTYLE dwg_document_first_mlinestyle(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->mlinestyle_first;
}

HMLINESTYLE dwg_document_next_mlinestyle(HMLINESTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->next;
}

unsigned long dwg_document_mlinestyle_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->mlinestyle_count;
}

/* ----------------------------------------------------------
   DIMENSION STYLES
   ---------------------------------------------------------- */

HDIMSTYLE dwg_document_add_dimstyle(HDWG hDwg,
                                    const char *name)
{
    HDIMSTYLE style;

    if (hDwg == NULL)
        return NULL;

    if (dwg_document_get_dimstyle(hDwg, name) != NULL)
        return NULL;

    style = dwg_dimstyle_create(name);

    if (style == NULL)
        return NULL;

    if (hDwg->dimstyle_first == NULL)
    {
        hDwg->dimstyle_first = style;
        hDwg->dimstyle_last = style;
    }
    else
    {
        style->prev = hDwg->dimstyle_last;

        hDwg->dimstyle_last->next = style;

        hDwg->dimstyle_last = style;
    }

    hDwg->dimstyle_count++;

    return style;
}

long dwg_document_remove_dimstyle(HDWG hDwg,
                                  HDIMSTYLE style)
{
    if (hDwg == NULL || style == NULL)
        return 0L;

    if (style->prev != NULL)
        style->prev->next = style->next;
    else
        hDwg->dimstyle_first = style->next;

    if (style->next != NULL)
        style->next->prev = style->prev;
    else
        hDwg->dimstyle_last = style->prev;

    if (hDwg->dimstyle_count > 0UL)
        hDwg->dimstyle_count--;

    style->next = NULL;
    style->prev = NULL;

    dwg_dimstyle_destroy(style);

    return 1L;
}

HDIMSTYLE dwg_document_get_dimstyle(HDWG hDwg,
                                    const char *name)
{
    HDIMSTYLE style;

    if (hDwg == NULL || name == NULL)
        return NULL;

    style = hDwg->dimstyle_first;

    while (style != NULL)
    {
        if (dwg_name_equal(dwg_dimstyle_get_name(style), name))
            return style;

        style = style->next;
    }

    return NULL;
}

HDIMSTYLE dwg_document_first_dimstyle(HDWG hDwg)
{
    if (hDwg == NULL)
        return NULL;

    return hDwg->dimstyle_first;
}

HDIMSTYLE dwg_document_next_dimstyle(HDIMSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->next;
}

unsigned long dwg_document_dimstyle_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->dimstyle_count;
}

DWG_BOOL dwg_document_sel_contains(HDWG hDwg, HENTITY entity)
{
    unsigned long i;

    if (hDwg == NULL || entity == NULL)
        return DWG_FALSE;

    for (i = 0UL; i < hDwg->sel_count; i++)
    {
        if (hDwg->sel_items[i] == entity)
            return DWG_TRUE;
    }

    return DWG_FALSE;
}

long dwg_document_sel_add(HDWG hDwg, HENTITY entity)
{
    if (hDwg == NULL || entity == NULL)
        return 0L;

    if (dwg_document_sel_contains(hDwg, entity))
        return 1L;

    if (hDwg->sel_count == hDwg->sel_capacity)
    {
        unsigned long new_capacity = (hDwg->sel_capacity == 0UL) ? 16UL : hDwg->sel_capacity * 2UL;
        HENTITY *new_items = (HENTITY *)realloc(hDwg->sel_items, (size_t)new_capacity * sizeof(HENTITY));

        if (new_items == NULL)
            return 0L;

        hDwg->sel_items = new_items;
        hDwg->sel_capacity = new_capacity;
    }

    hDwg->sel_items[hDwg->sel_count] = entity;
    hDwg->sel_count++;

    return 1L;
}

void dwg_document_sel_clear(HDWG hDwg)
{
    if (hDwg == NULL)
        return;

    hDwg->sel_count = 0UL; /* keep the allocated buffer, just reset the count */
}

unsigned long dwg_document_sel_count(HDWG hDwg)
{
    if (hDwg == NULL)
        return 0UL;

    return hDwg->sel_count;
}

HENTITY dwg_document_sel_get(HDWG hDwg, unsigned long index)
{
    if (hDwg == NULL || index >= hDwg->sel_count)
        return NULL;

    return hDwg->sel_items[index];
}