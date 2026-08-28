#ifndef DWG_ENTITY_H
#define DWG_ENTITY_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _DWG_ENTITY
{
    DWG_ENTITY_TYPE type;
    DWG_ID id;

    unsigned short color;
    unsigned short flags;

    char layer[64];
    char linetype[64];

    void *user_data;

    unsigned char *ex_data;
    unsigned long ex_data_size;

    void *geometry;

    DWG_ENTITY *next;
    DWG_ENTITY *prev;
};

HENTITY dwg_entity_create(HDWG hDwg, DWG_ENTITY_TYPE type);
void dwg_entity_destroy(HENTITY hEntity);

DWG_ID dwg_entity_get_id(HENTITY hEntity);
DWG_ENTITY_TYPE dwg_entity_get_type(HENTITY hEntity);

long dwg_entity_put_color(HENTITY hEntity, unsigned short color);
unsigned short dwg_entity_get_color(HENTITY hEntity);

long dwg_entity_put_layer(HENTITY hEntity, const char *name);
const char *dwg_entity_get_layer(HENTITY hEntity);

long dwg_entity_put_linetype(HENTITY hEntity, const char *name);
const char *dwg_entity_get_linetype(HENTITY hEntity);

long dwg_entity_put_user_data(HENTITY hEntity, void *user_data);
void *dwg_entity_get_user_data(HENTITY hEntity);

long dwg_entity_put_ex_data(HENTITY hEntity, const void *data,
                            unsigned long size);
const void *dwg_entity_get_ex_data(HENTITY hEntity,
                                   unsigned long *size);

/* Selection flag for visual highlighting in the viewer */
#define DWG_ENTITY_FLAG_SELECTED  0x0001

void dwg_entity_set_selected(HENTITY hEntity, int selected);
int dwg_entity_is_selected(HENTITY hEntity);

#ifdef __cplusplus
}
#endif

#endif
