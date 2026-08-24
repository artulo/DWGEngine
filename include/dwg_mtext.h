#ifndef DWG_MTEXT_H
#define DWG_MTEXT_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_MTEXT_MAX 2048

#define DWG_MTEXT_ATTACH_TOP_LEFT      1
#define DWG_MTEXT_ATTACH_TOP_CENTER    2
#define DWG_MTEXT_ATTACH_TOP_RIGHT     3
#define DWG_MTEXT_ATTACH_MIDDLE_LEFT   4
#define DWG_MTEXT_ATTACH_MIDDLE_CENTER 5
#define DWG_MTEXT_ATTACH_MIDDLE_RIGHT  6
#define DWG_MTEXT_ATTACH_BOTTOM_LEFT   7
#define DWG_MTEXT_ATTACH_BOTTOM_CENTER 8
#define DWG_MTEXT_ATTACH_BOTTOM_RIGHT  9

/*
 * Recovered from real vecad.dll's CEntMText (FUN_1008e810 @
 * 0x1008e810, checked 2026-08-19/20): confirmed insertion point and an
 * inline text buffer (64 bytes there; widened here since MTEXT content
 * is routinely longer than TEXT's). CEntMText showed five separate
 * zeroed 3-double blocks in the constructor -- more than a simple
 * point+direction pair would need -- but which double belongs to which
 * DXF-equivalent field (reference rect width, line spacing, rotation
 * angle vs. direction vector, attachment point) was not pinned down
 * with confidence. Modeled here against vecad.h's public
 * CadAddMText / CadMTextGet.../Put... API surface and standard DXF MTEXT
 * semantics instead of the uncertain offsets.
 */
typedef struct _DWG_MTEXT
{
    DWG_POINT3D point;

    char text[DWG_MTEXT_MAX];
    char style_name[64];

    double height;
    double rect_width;
    double angle;
    double line_space;

    unsigned short attach;
} DWG_MTEXT;

HENTITY dwg_add_mtext(HDWG hDwg,
                      double x, double y, double z,
                      double height,
                      double rect_width,
                      const char *text);

const char *dwg_mtext_get_text(HENTITY hEntity);
long dwg_mtext_set_text(HENTITY hEntity, const char *text);

const char *dwg_mtext_get_style_name(HENTITY hEntity);
long dwg_mtext_set_style_name(HENTITY hEntity, const char *style_name);

void dwg_mtext_get_point(HENTITY hEntity, double *x, double *y, double *z);
void dwg_mtext_set_point(HENTITY hEntity, double x, double y, double z);

double dwg_mtext_get_height(HENTITY hEntity);
long dwg_mtext_set_height(HENTITY hEntity, double height);

double dwg_mtext_get_rect_width(HENTITY hEntity);
long dwg_mtext_set_rect_width(HENTITY hEntity, double rect_width);

double dwg_mtext_get_angle(HENTITY hEntity);
long dwg_mtext_set_angle(HENTITY hEntity, double angle);

double dwg_mtext_get_line_space(HENTITY hEntity);
long dwg_mtext_set_line_space(HENTITY hEntity, double line_space);

unsigned short dwg_mtext_get_attach(HENTITY hEntity);
long dwg_mtext_set_attach(HENTITY hEntity, unsigned short attach);

#ifdef __cplusplus
}
#endif

#endif
