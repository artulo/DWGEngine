#ifndef DWG_POINTSTYLE_H
#define DWG_POINTSTYLE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_POINTSTYLE_NAME_MAX 64

/*
 * Defaults/validation ranges match real vecad.dll's CPntStyle
 * constructor (FUN_100a9fe0 @ 0x100a9fe0, checked 2026-08-20):
 * block_scale defaults 1.0, validated (0.0001, 10000.0]; text_height
 * defaults 2.5 (same ISO default seen on CTextStyle); text_width
 * defaults 1.0, validated [0.1, 5.0] (identical range to CTextStyle's
 * width factor); snap defaults to 3.
 */
#define DWG_POINTSTYLE_BLOCK_SCALE_MIN   0.0001
#define DWG_POINTSTYLE_BLOCK_SCALE_MAX   10000.0
#define DWG_POINTSTYLE_TEXT_HEIGHT_MIN   0.0001
#define DWG_POINTSTYLE_TEXT_HEIGHT_MAX   100000.0
#define DWG_POINTSTYLE_TEXT_WIDTH_MIN    0.1
#define DWG_POINTSTYLE_TEXT_WIDTH_MAX    5.0
#define DWG_POINTSTYLE_DEFAULT_TEXT_HEIGHT 2.5
#define DWG_POINTSTYLE_DEFAULT_BLOCK_SCALE 1.0
#define DWG_POINTSTYLE_DEFAULT_TEXT_WIDTH  1.0
#define DWG_POINTSTYLE_DEFAULT_SNAP        3

typedef struct _DWG_POINTSTYLE DWG_POINTSTYLE;
typedef DWG_POINTSTYLE * HPOINTSTYLE;

struct _DWG_POINTSTYLE
{
    char name[DWG_POINTSTYLE_NAME_MAX];

    long block_id;
    double block_scale;

    long font;
    DWG_BOOL fixed;

    double text_height;
    double text_width;

    unsigned char snap;

    HPOINTSTYLE next;
    HPOINTSTYLE prev;
};

HPOINTSTYLE dwg_pointstyle_create(const char *name);
void dwg_pointstyle_destroy(HPOINTSTYLE style);

const char *dwg_pointstyle_get_name(HPOINTSTYLE style);
long dwg_pointstyle_set_name(HPOINTSTYLE style, const char *name);

long dwg_pointstyle_get_block_id(HPOINTSTYLE style);
long dwg_pointstyle_set_block_id(HPOINTSTYLE style, long block_id);

/* Clamped/ignored per real vecad's validation -- see the ranges above. */
double dwg_pointstyle_get_block_scale(HPOINTSTYLE style);
long dwg_pointstyle_set_block_scale(HPOINTSTYLE style, double scale);

long dwg_pointstyle_get_font(HPOINTSTYLE style);
long dwg_pointstyle_set_font(HPOINTSTYLE style, long font);

DWG_BOOL dwg_pointstyle_get_fixed(HPOINTSTYLE style);
long dwg_pointstyle_set_fixed(HPOINTSTYLE style, DWG_BOOL value);

double dwg_pointstyle_get_text_height(HPOINTSTYLE style);
long dwg_pointstyle_set_text_height(HPOINTSTYLE style, double height);

double dwg_pointstyle_get_text_width(HPOINTSTYLE style);
long dwg_pointstyle_set_text_width(HPOINTSTYLE style, double width);

unsigned char dwg_pointstyle_get_snap(HPOINTSTYLE style);
long dwg_pointstyle_set_snap(HPOINTSTYLE style, unsigned char snap);

#ifdef __cplusplus
}
#endif

#endif
