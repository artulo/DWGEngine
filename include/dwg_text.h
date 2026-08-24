#ifndef DWG_TEXT_H
#define DWG_TEXT_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_TEXT_MAX 256

#define DWG_TEXT_ALIGN_LEFT   0
#define DWG_TEXT_ALIGN_CENTER 1
#define DWG_TEXT_ALIGN_RIGHT  2

/*
 * Fields match real vecad.dll's CEntText, recovered in
 * D:\estudio\DWGEngine\reverse\CEntText_notes.md: an insertion point AND
 * a separate alignment point (both confirmed), plus an inline text
 * buffer. Height/angle/width-factor/oblique offsets were not confirmed
 * with certainty from the decompile -- default values here follow
 * AutoCAD's own well-known conventions (width factor 1.0, oblique 0.0)
 * rather than an unverified guessed offset.
 */
typedef struct _DWG_TEXT
{
    DWG_POINT3D point;
    DWG_POINT3D point0;

    char text[DWG_TEXT_MAX];
    char style_name[64];

    double height;
    double angle;
    double width_factor;
    double oblique;

    unsigned short align;

    DWG_BOOL backward;
    DWG_BOOL upside_down;
} DWG_TEXT;

HENTITY dwg_add_text(HDWG hDwg,
                     double x, double y, double z,
                     double height,
                     double angle,
                     const char *text);

const char *dwg_text_get_text(HENTITY hEntity);
long dwg_text_set_text(HENTITY hEntity, const char *text);

const char *dwg_text_get_style_name(HENTITY hEntity);
long dwg_text_set_style_name(HENTITY hEntity, const char *style_name);

void dwg_text_get_point(HENTITY hEntity, double *x, double *y, double *z);
void dwg_text_set_point(HENTITY hEntity, double x, double y, double z);

void dwg_text_get_point0(HENTITY hEntity, double *x, double *y, double *z);
void dwg_text_set_point0(HENTITY hEntity, double x, double y, double z);

double dwg_text_get_height(HENTITY hEntity);
long dwg_text_set_height(HENTITY hEntity, double height);

double dwg_text_get_angle(HENTITY hEntity);
long dwg_text_set_angle(HENTITY hEntity, double angle);

double dwg_text_get_width_factor(HENTITY hEntity);
long dwg_text_set_width_factor(HENTITY hEntity, double factor);

double dwg_text_get_oblique(HENTITY hEntity);
long dwg_text_set_oblique(HENTITY hEntity, double oblique);

unsigned short dwg_text_get_align(HENTITY hEntity);
long dwg_text_set_align(HENTITY hEntity, unsigned short align);

DWG_BOOL dwg_text_get_backward(HENTITY hEntity);
long dwg_text_set_backward(HENTITY hEntity, DWG_BOOL value);

DWG_BOOL dwg_text_get_upside_down(HENTITY hEntity);
long dwg_text_set_upside_down(HENTITY hEntity, DWG_BOOL value);

#ifdef __cplusplus
}
#endif

#endif
