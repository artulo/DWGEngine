#ifndef DWG_LAYER_H
#define DWG_LAYER_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_LAYER_NAME_MAX 64

#define DWG_LAYER_OFF       0x0001UL
#define DWG_LAYER_FROZEN    0x0002UL
#define DWG_LAYER_LOCKED    0x0004UL
#define DWG_LAYER_PLOTTABLE 0x0008UL

typedef struct _DWG_LAYER DWG_LAYER;
typedef DWG_LAYER * HLAYER;

struct _DWG_LAYER
{
    char name[DWG_LAYER_NAME_MAX];

    unsigned short color;
    unsigned short flags;

    char linetype[64];

    HLAYER next;
    HLAYER prev;
};

HLAYER dwg_layer_create(const char *name);
void dwg_layer_destroy(HLAYER layer);

const char *dwg_layer_get_name(HLAYER layer);
long dwg_layer_set_name(HLAYER layer, const char *name);

unsigned short dwg_layer_get_color(HLAYER layer);
long dwg_layer_set_color(HLAYER layer, unsigned short color);

const char *dwg_layer_get_linetype(HLAYER layer);
long dwg_layer_set_linetype(HLAYER layer, const char *name);

unsigned short dwg_layer_get_flags(HLAYER layer);
long dwg_layer_set_flags(HLAYER layer, unsigned short flags);

long dwg_layer_is_off(HLAYER layer);
long dwg_layer_is_frozen(HLAYER layer);
long dwg_layer_is_locked(HLAYER layer);
long dwg_layer_is_plottable(HLAYER layer);

long dwg_layer_set_off(HLAYER layer, long value);
long dwg_layer_set_frozen(HLAYER layer, long value);
long dwg_layer_set_locked(HLAYER layer, long value);
long dwg_layer_set_plottable(HLAYER layer, long value);

#ifdef __cplusplus
}
#endif

#endif