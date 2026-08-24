#ifndef DWG_LINETYPE_H
#define DWG_LINETYPE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_LINETYPE_NAME_MAX 64
#define DWG_LINETYPE_DESCR_MAX 128

typedef struct _DWG_LINETYPE DWG_LINETYPE;
typedef DWG_LINETYPE * HLINETYPE;

/*
 * Real vecad.dll's CLinetype (see D:\estudio\DWGEngine\reverse, checked
 * 2026-08-19) also carries a 14-segment dash-pattern array (matches the
 * R12 DXF grammar's double[13] ltype pattern) -- not modeled here.
 * Entities already reference a linetype purely by name string
 * (dwg_entity_get/put_linetype), so a named table entry with a
 * description is what's actually needed for DXF round-tripping today;
 * the dash-pattern array would only matter for accurate rendering,
 * which this engine doesn't do yet.
 */
struct _DWG_LINETYPE
{
    char name[DWG_LINETYPE_NAME_MAX];
    char description[DWG_LINETYPE_DESCR_MAX];

    HLINETYPE next;
    HLINETYPE prev;
};

HLINETYPE dwg_linetype_create(const char *name);
void dwg_linetype_destroy(HLINETYPE linetype);

const char *dwg_linetype_get_name(HLINETYPE linetype);
long dwg_linetype_set_name(HLINETYPE linetype, const char *name);

const char *dwg_linetype_get_descr(HLINETYPE linetype);
long dwg_linetype_set_descr(HLINETYPE linetype, const char *descr);

#ifdef __cplusplus
}
#endif

#endif
