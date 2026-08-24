#ifndef DWG_INSERT_H
#define DWG_INSERT_H

#include "dwg_types.h"
#include "dwg_entity.h"
#include "dwg_block.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A DWG_ENTITY_INSERT entity references a DWG_BLOCK definition by name
 * (matching DXF's own INSERT entity, group 2 = block name) and places
 * it at a point with per-axis scale and a rotation angle. Matches
 * vecad.h's CadAddInsBlock / CadInsBlockGet.../Put... API surface.
 */
typedef struct _DWG_INSERT
{
    char block_name[DWG_BLOCK_NAME_MAX];

    DWG_POINT3D point;

    double scale_x;
    double scale_y;
    double scale_z;

    double angle; /* degrees */
} DWG_INSERT;

HENTITY dwg_add_insert(HDWG hDwg,
                       const char *block_name,
                       double x, double y, double z,
                       double angle);

const char *dwg_insert_get_block_name(HENTITY hEntity);
long dwg_insert_set_block_name(HENTITY hEntity, const char *block_name);

void dwg_insert_get_point(HENTITY hEntity, double *x, double *y, double *z);
void dwg_insert_set_point(HENTITY hEntity, double x, double y, double z);

void dwg_insert_get_scale(HENTITY hEntity, double *sx, double *sy, double *sz);
void dwg_insert_set_scale(HENTITY hEntity, double sx, double sy, double sz);

double dwg_insert_get_angle(HENTITY hEntity);
long dwg_insert_set_angle(HENTITY hEntity, double angle);

#ifdef __cplusplus
}
#endif

#endif
