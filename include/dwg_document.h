#ifndef DWG_DOCUMENT_H
#define DWG_DOCUMENT_H

#include "dwg_types.h"
#include "dwg_layer.h"
#include "dwg_page.h"
#include "dwg_block.h"
#include "dwg_linetype.h"
#include "dwg_style.h"
#include "dwg_pointstyle.h"
#include "dwg_mlinestyle.h"
#include "dwg_dimstyle.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _DWG_DOCUMENT
{
    DWG_ENTITY *entity_first;
    DWG_ENTITY *entity_last;

    unsigned long entity_count;

    DWG_ID next_entity_id;

    HLAYER layer_first;
    HLAYER layer_last;

    unsigned long layer_count;

    HPAGE page_first;
    HPAGE page_last;

    unsigned long page_count;

    HBLOCK block_first;
    HBLOCK block_last;

    unsigned long block_count;

    HLINETYPE linetype_first;
    HLINETYPE linetype_last;

    unsigned long linetype_count;

    HSTYLE style_first;
    HSTYLE style_last;

    unsigned long style_count;

    HPOINTSTYLE pointstyle_first;
    HPOINTSTYLE pointstyle_last;

    unsigned long pointstyle_count;

    HMLINESTYLE mlinestyle_first;
    HMLINESTYLE mlinestyle_last;

    unsigned long mlinestyle_count;

    HDIMSTYLE dimstyle_first;
    HDIMSTYLE dimstyle_last;

    unsigned long dimstyle_count;

    /*
     * Current selection set -- mirrors real vecad.dll's global CadSel*
     * concept (CadSelectByDist/ByPolygon/ByLayer add to it, CadSelMove/
     * Rotate/.../Erase/Explode/Copy act on it, CadSelClear/CadSelCount
     * manage it), but scoped to the document instead of process-global.
     * A plain array of borrowed HENTITY pointers (the selection doesn't
     * own these entities -- entity_first/entity_last does); see
     * dwg_selection.h for the operations built on top of this.
     */
    HENTITY *sel_items;
    unsigned long sel_count;
    unsigned long sel_capacity;
};

HDWG dwg_document_create(void);
void dwg_document_destroy(HDWG hDwg);

HENTITY dwg_document_add_entity(HDWG hDwg,
                                 DWG_ENTITY_TYPE type);

long dwg_document_remove_entity(HDWG hDwg,
                                HENTITY hEntity);

/*
 * Unlinks hEntity from hDwg's main entity list WITHOUT destroying it
 * (unlike dwg_document_remove_entity) -- for relocating an entity into a
 * block's own entity list, e.g. while reading a DXF BLOCKS section.
 */
HENTITY dwg_document_detach_entity(HDWG hDwg,
                                   HENTITY hEntity);

HENTITY dwg_document_get_entity_by_id(HDWG hDwg,
                                       DWG_ID id);

HENTITY dwg_document_first_entity(HDWG hDwg);
HENTITY dwg_document_next_entity(HENTITY hEntity);

unsigned long dwg_document_entity_count(HDWG hDwg);

HLAYER dwg_document_add_layer(HDWG hDwg,
                              const char *name);

long dwg_document_remove_layer(HDWG hDwg,
                               HLAYER layer);

HLAYER dwg_document_get_layer(HDWG hDwg,
                               const char *name);

HLAYER dwg_document_first_layer(HDWG hDwg);
HLAYER dwg_document_next_layer(HLAYER layer);

unsigned long dwg_document_layer_count(HDWG hDwg);

HPAGE dwg_document_add_page(HDWG hDwg,
                            const char *name);

long dwg_document_remove_page(HDWG hDwg,
                              HPAGE page);

HPAGE dwg_document_get_page(HDWG hDwg,
                            const char *name);

HPAGE dwg_document_first_page(HDWG hDwg);
HPAGE dwg_document_next_page(HPAGE page);

unsigned long dwg_document_page_count(HDWG hDwg);

HBLOCK dwg_document_add_block(HDWG hDwg,
                              const char *name);

long dwg_document_remove_block(HDWG hDwg,
                               HBLOCK block);

HBLOCK dwg_document_get_block(HDWG hDwg,
                              const char *name);

HBLOCK dwg_document_first_block(HDWG hDwg);
HBLOCK dwg_document_next_block(HBLOCK block);

unsigned long dwg_document_block_count(HDWG hDwg);

HLINETYPE dwg_document_add_linetype(HDWG hDwg,
                                    const char *name);

long dwg_document_remove_linetype(HDWG hDwg,
                                  HLINETYPE linetype);

HLINETYPE dwg_document_get_linetype(HDWG hDwg,
                                    const char *name);

HLINETYPE dwg_document_first_linetype(HDWG hDwg);
HLINETYPE dwg_document_next_linetype(HLINETYPE linetype);

unsigned long dwg_document_linetype_count(HDWG hDwg);

HSTYLE dwg_document_add_style(HDWG hDwg,
                              const char *name);

long dwg_document_remove_style(HDWG hDwg,
                               HSTYLE style);

HSTYLE dwg_document_get_style(HDWG hDwg,
                              const char *name);

HSTYLE dwg_document_first_style(HDWG hDwg);
HSTYLE dwg_document_next_style(HSTYLE style);

unsigned long dwg_document_style_count(HDWG hDwg);

HPOINTSTYLE dwg_document_add_pointstyle(HDWG hDwg,
                                        const char *name);

long dwg_document_remove_pointstyle(HDWG hDwg,
                                    HPOINTSTYLE style);

HPOINTSTYLE dwg_document_get_pointstyle(HDWG hDwg,
                                        const char *name);

HPOINTSTYLE dwg_document_first_pointstyle(HDWG hDwg);
HPOINTSTYLE dwg_document_next_pointstyle(HPOINTSTYLE style);

unsigned long dwg_document_pointstyle_count(HDWG hDwg);

HMLINESTYLE dwg_document_add_mlinestyle(HDWG hDwg,
                                        const char *name);

long dwg_document_remove_mlinestyle(HDWG hDwg,
                                    HMLINESTYLE style);

HMLINESTYLE dwg_document_get_mlinestyle(HDWG hDwg,
                                        const char *name);

HMLINESTYLE dwg_document_first_mlinestyle(HDWG hDwg);
HMLINESTYLE dwg_document_next_mlinestyle(HMLINESTYLE style);

unsigned long dwg_document_mlinestyle_count(HDWG hDwg);

HDIMSTYLE dwg_document_add_dimstyle(HDWG hDwg,
                                    const char *name);

long dwg_document_remove_dimstyle(HDWG hDwg,
                                  HDIMSTYLE style);

HDIMSTYLE dwg_document_get_dimstyle(HDWG hDwg,
                                    const char *name);

HDIMSTYLE dwg_document_first_dimstyle(HDWG hDwg);
HDIMSTYLE dwg_document_next_dimstyle(HDIMSTYLE style);

unsigned long dwg_document_dimstyle_count(HDWG hDwg);

/*
 * Low-level selection-set storage (see the struct comment above and
 * dwg_selection.h for the pick/window/batch-edit operations built on
 * top of these). dwg_document_sel_add is idempotent -- adding an
 * already-selected entity is a no-op, not a duplicate.
 */
long dwg_document_sel_add(HDWG hDwg, HENTITY entity);
void dwg_document_sel_clear(HDWG hDwg);
unsigned long dwg_document_sel_count(HDWG hDwg);
HENTITY dwg_document_sel_get(HDWG hDwg, unsigned long index);
DWG_BOOL dwg_document_sel_contains(HDWG hDwg, HENTITY entity);

#ifdef __cplusplus
}
#endif

#endif