#ifndef DWG_LEADER_H
#define DWG_LEADER_H

#include "dwg_types.h"
#include "dwg_entity.h"
#include "dwg_vertex.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_LEADER_TEXT_MAX 256

/*
 * Recovered from real vecad.dll's CEntLeader (FUN_1008a0f0 @
 * 0x1008a0f0, checked 2026-08-20): a point-array pattern (count field +
 * 200-byte memset'd buffer) similar in shape to CEntPolyline's vertex
 * array, plus several more zeroed fields whose individual meaning
 * wasn't pinned down. Modeled pragmatically against vecad.h's public
 * CadAddLeader/CadLeaderGet.../Put... API (arrow size, spline flag,
 * text + text height) instead, reusing the existing HVERTEX list for
 * the leader's point sequence -- same approach already used for
 * CEntPolyline and CEntHatch's boundary.
 */
typedef struct _DWG_LEADER
{
    HVERTEX vertex_first;
    HVERTEX vertex_last;
    unsigned long vertex_count;

    double arrow_size;
    DWG_BOOL spline;

    char text[DWG_LEADER_TEXT_MAX];
    double text_height;
} DWG_LEADER;

HENTITY dwg_add_leader(HDWG hDwg);

HVERTEX dwg_leader_add_vertex(HENTITY hEntity, double x, double y, double z);

HVERTEX dwg_leader_first_vertex(HENTITY hEntity);
HVERTEX dwg_leader_next_vertex(HVERTEX vertex);

unsigned long dwg_leader_vertex_count(HENTITY hEntity);

double dwg_leader_get_arrow_size(HENTITY hEntity);
long dwg_leader_set_arrow_size(HENTITY hEntity, double size);

DWG_BOOL dwg_leader_get_spline(HENTITY hEntity);
long dwg_leader_set_spline(HENTITY hEntity, DWG_BOOL spline);

const char *dwg_leader_get_text(HENTITY hEntity);
long dwg_leader_set_text(HENTITY hEntity, const char *text);

double dwg_leader_get_text_height(HENTITY hEntity);
long dwg_leader_set_text_height(HENTITY hEntity, double height);

#ifdef __cplusplus
}
#endif

#endif
