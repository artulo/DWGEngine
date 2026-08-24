#ifndef DWG_DIMSTYLE_H
#define DWG_DIMSTYLE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_DIMSTYLE_NAME_MAX 64

/*
 * CDimStyle (real vecad.dll) is a large table record (~488 bytes, 20+
 * properties per vecad.h's CadDimStyleGet.../Put... API) -- checked
 * 2026-08-20 (FUN_100a3e10 @ 0x100a3e10, the real default-value
 * initializer, found via a "create new" constructor path rather than
 * the copy-constructor most other CDimStyle constructors use). Several
 * defaults decoded precisely (Python struct.unpack, not hand hex-math):
 * one field defaults to exactly 25.4 (1 inch in mm), several to 2.5
 * (the same ISO text-height default seen on CTextStyle/CPntStyle), one
 * to 1.0 (matches AutoCAD's own DIMSCALE default). Scoped down here to
 * the handful of properties confidently identifiable this way, rather
 * than force-mapping all 20 vecad.h properties to offsets that would
 * need each individual getter traced to confirm -- same approach
 * already used for dwg_linetype.
 */
#define DWG_DIMSTYLE_DEFAULT_TEXT_HEIGHT 2.5
#define DWG_DIMSTYLE_DEFAULT_ARROW_SIZE  2.5
#define DWG_DIMSTYLE_DEFAULT_SCALE       1.0
#define DWG_DIMSTYLE_DEFAULT_EXT_OFFSET  0.38

typedef struct _DWG_DIMSTYLE DWG_DIMSTYLE;
typedef DWG_DIMSTYLE * HDIMSTYLE;

struct _DWG_DIMSTYLE
{
    char name[DWG_DIMSTYLE_NAME_MAX];

    double text_height;
    double arrow_size;
    double scale;
    double ext_offset;

    unsigned short precision;

    HDIMSTYLE next;
    HDIMSTYLE prev;
};

HDIMSTYLE dwg_dimstyle_create(const char *name);
void dwg_dimstyle_destroy(HDIMSTYLE style);

const char *dwg_dimstyle_get_name(HDIMSTYLE style);
long dwg_dimstyle_set_name(HDIMSTYLE style, const char *name);

double dwg_dimstyle_get_text_height(HDIMSTYLE style);
long dwg_dimstyle_set_text_height(HDIMSTYLE style, double height);

double dwg_dimstyle_get_arrow_size(HDIMSTYLE style);
long dwg_dimstyle_set_arrow_size(HDIMSTYLE style, double size);

double dwg_dimstyle_get_scale(HDIMSTYLE style);
long dwg_dimstyle_set_scale(HDIMSTYLE style, double scale);

double dwg_dimstyle_get_ext_offset(HDIMSTYLE style);
long dwg_dimstyle_set_ext_offset(HDIMSTYLE style, double offset);

unsigned short dwg_dimstyle_get_precision(HDIMSTYLE style);
long dwg_dimstyle_set_precision(HDIMSTYLE style, unsigned short precision);

#ifdef __cplusplus
}
#endif

#endif
