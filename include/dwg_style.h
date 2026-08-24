#ifndef DWG_STYLE_H
#define DWG_STYLE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_STYLE_NAME_MAX 64
#define DWG_STYLE_FONT_MAX 260

/*
 * Defaults/validation ranges match real vecad.dll's CTextStyle
 * constructor (FUN_100aa510 @ 0x100aa510, checked 2026-08-19/20):
 * oblique clamped to +-70 degrees (1.2217304763960308 rad exactly),
 * width factor clamped to [0.1, 5.0] with 0 treated as "use default"
 * (1.0), height defaulting to 2.5 -- a standard ISO technical-drawing
 * text height.
 */
#define DWG_STYLE_OBLIQUE_MAX  70.0
#define DWG_STYLE_WIDTH_MIN     0.1
#define DWG_STYLE_WIDTH_MAX     5.0
#define DWG_STYLE_DEFAULT_HEIGHT 2.5
#define DWG_STYLE_DEFAULT_WIDTH  1.0

typedef struct _DWG_STYLE DWG_STYLE;
typedef DWG_STYLE * HSTYLE;

struct _DWG_STYLE
{
    char name[DWG_STYLE_NAME_MAX];
    char font_name[DWG_STYLE_FONT_MAX];
    char ttf_name[DWG_STYLE_FONT_MAX];

    double height;
    double width_factor;
    double oblique;

    DWG_BOOL backward;
    DWG_BOOL upside_down;

    HSTYLE next;
    HSTYLE prev;
};

HSTYLE dwg_style_create(const char *name);
void dwg_style_destroy(HSTYLE style);

const char *dwg_style_get_name(HSTYLE style);
long dwg_style_set_name(HSTYLE style, const char *name);

const char *dwg_style_get_font(HSTYLE style);
long dwg_style_set_font(HSTYLE style, const char *font_name);

const char *dwg_style_get_ttf_name(HSTYLE style);
long dwg_style_set_ttf_name(HSTYLE style, const char *ttf_name);

double dwg_style_get_height(HSTYLE style);
long dwg_style_set_height(HSTYLE style, double height);

/* Clamped to [DWG_STYLE_WIDTH_MIN, DWG_STYLE_WIDTH_MAX]; 0 resets to
   DWG_STYLE_DEFAULT_WIDTH, matching real vecad's own validation. */
double dwg_style_get_width_factor(HSTYLE style);
long dwg_style_set_width_factor(HSTYLE style, double width_factor);

/* Clamped to +-DWG_STYLE_OBLIQUE_MAX degrees; out-of-range values are
   ignored (the field keeps its previous value), matching real vecad. */
double dwg_style_get_oblique(HSTYLE style);
long dwg_style_set_oblique(HSTYLE style, double oblique);

DWG_BOOL dwg_style_get_backward(HSTYLE style);
long dwg_style_set_backward(HSTYLE style, DWG_BOOL value);

DWG_BOOL dwg_style_get_upside_down(HSTYLE style);
long dwg_style_set_upside_down(HSTYLE style, DWG_BOOL value);

#ifdef __cplusplus
}
#endif

#endif
