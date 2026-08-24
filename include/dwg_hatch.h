#ifndef DWG_HATCH_H
#define DWG_HATCH_H

#include "dwg_types.h"
#include "dwg_entity.h"
#include "dwg_vertex.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_HATCH_PATTERN_MAX 64

/*
 * Recovered from real vecad.dll's CEntHatch (FUN_10081ba0 @
 * 0x10081ba0, checked 2026-08-20): a copy constructor revealed a
 * pattern-name string, an angle/scale pair, and TWO separate flat
 * arrays -- a 16-byte-per-record "boundary path" array (count+pointer)
 * and a 4-byte-per-record secondary array (likely raw boundary point
 * coordinates or pattern line data) -- considerably richer than what's
 * modeled here. Scoped down to what vecad.h's public
 * CadAddHatch/CadHatchGet.../Put... API and our own boundary-drawing
 * needs actually require: pattern name, angle, scale, a solid-fill
 * flag, and ONE boundary loop reusing the existing HVERTEX list (the
 * same node type dwg_polyline.h already uses) rather than inventing a
 * second flat-array representation to match vecad's internals exactly.
 */
typedef struct _DWG_HATCH
{
    char pattern_name[DWG_HATCH_PATTERN_MAX];

    double angle;
    double scale;

    DWG_BOOL solid;

    HVERTEX boundary_first;
    HVERTEX boundary_last;
    unsigned long boundary_count;
} DWG_HATCH;

HENTITY dwg_add_hatch(HDWG hDwg,
                      const char *pattern_name,
                      double angle,
                      double scale,
                      DWG_BOOL solid);

const char *dwg_hatch_get_pattern(HENTITY hEntity);
long dwg_hatch_set_pattern(HENTITY hEntity, const char *pattern_name);

double dwg_hatch_get_angle(HENTITY hEntity);
long dwg_hatch_set_angle(HENTITY hEntity, double angle);

double dwg_hatch_get_scale(HENTITY hEntity);
long dwg_hatch_set_scale(HENTITY hEntity, double scale);

DWG_BOOL dwg_hatch_get_solid(HENTITY hEntity);
long dwg_hatch_set_solid(HENTITY hEntity, DWG_BOOL solid);

HVERTEX dwg_hatch_add_boundary_point(HENTITY hEntity, double x, double y, double z);

HVERTEX dwg_hatch_first_boundary_point(HENTITY hEntity);
HVERTEX dwg_hatch_next_boundary_point(HVERTEX vertex);

unsigned long dwg_hatch_boundary_count(HENTITY hEntity);

#ifdef __cplusplus
}
#endif

#endif
