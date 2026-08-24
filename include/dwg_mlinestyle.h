#ifndef DWG_MLINESTYLE_H
#define DWG_MLINESTYLE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_MLINESTYLE_NAME_MAX 64
#define DWG_MLINESTYLE_MAX_LINES 16

/*
 * Recovered from real vecad.dll's CMlineStyle (FUN_100a9090 @
 * 0x100a9090, checked 2026-08-20): inherits CTableRec, then stores a
 * small array of "line" definitions -- each with an offset (double),
 * a color defaulting to 256 (BYLAYER, same convention seen everywhere
 * else in vecad) and a linetype reference. Matches vecad.h's public
 * CadMlineStyleGetNumLines/GetOffset/GetColor/GetLtypeID API. Scoped to
 * a small fixed-size array here rather than replicating the exact
 * dynamic-allocation shape -- multiline styles rarely need more than a
 * handful of parallel lines.
 */
typedef struct _DWG_MLINESTYLE DWG_MLINESTYLE;
typedef DWG_MLINESTYLE * HMLINESTYLE;

typedef struct _DWG_MLINE_ELEMENT
{
    double offset;
    unsigned short color;
    char linetype_name[64];
} DWG_MLINE_ELEMENT;

struct _DWG_MLINESTYLE
{
    char name[DWG_MLINESTYLE_NAME_MAX];

    DWG_MLINE_ELEMENT lines[DWG_MLINESTYLE_MAX_LINES];
    unsigned long line_count;

    HMLINESTYLE next;
    HMLINESTYLE prev;
};

HMLINESTYLE dwg_mlinestyle_create(const char *name);
void dwg_mlinestyle_destroy(HMLINESTYLE style);

const char *dwg_mlinestyle_get_name(HMLINESTYLE style);
long dwg_mlinestyle_set_name(HMLINESTYLE style, const char *name);

/* Returns 0 if DWG_MLINESTYLE_MAX_LINES was already reached. */
long dwg_mlinestyle_add_line(HMLINESTYLE style,
                             double offset,
                             unsigned short color,
                             const char *linetype_name);

unsigned long dwg_mlinestyle_line_count(HMLINESTYLE style);

double dwg_mlinestyle_get_offset(HMLINESTYLE style, unsigned long index);
unsigned short dwg_mlinestyle_get_color(HMLINESTYLE style, unsigned long index);
const char *dwg_mlinestyle_get_linetype(HMLINESTYLE style, unsigned long index);

#ifdef __cplusplus
}
#endif

#endif
