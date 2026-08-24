#ifndef DWG_SOLID_H
#define DWG_SOLID_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SOLID (filled quadrilateral) and 3DFACE (quad face outline) both
 * confirmed in real vecad.dll (CEntSolid/CEntFace, FUN_1009d2f0 @
 * 0x1009d2f0 and FUN_10081530 @ 0x10081530, checked 2026-08-20) to be
 * exactly 4 points (12 doubles) -- matches the DXF SOLID/3DFACE groups
 * 10/11/12/13 and the R12 binary grammar's identical 4-point shape for
 * both kinds. A 3-point triangle is the common degenerate case
 * (point4 == point3), same convention DXF itself uses.
 */
typedef struct _DWG_SOLID3D
{
    DWG_POINT3D p1;
    DWG_POINT3D p2;
    DWG_POINT3D p3;
    DWG_POINT3D p4;
} DWG_SOLID3D;

HENTITY dwg_add_solid(HDWG hDwg,
                      double x1, double y1, double z1,
                      double x2, double y2, double z2,
                      double x3, double y3, double z3,
                      double x4, double y4, double z4);

typedef struct _DWG_FACE3D
{
    DWG_POINT3D p1;
    DWG_POINT3D p2;
    DWG_POINT3D p3;
    DWG_POINT3D p4;

    unsigned short edge_flags;
} DWG_FACE3D;

HENTITY dwg_add_face(HDWG hDwg,
                     double x1, double y1, double z1,
                     double x2, double y2, double z2,
                     double x3, double y3, double z3,
                     double x4, double y4, double z4);

unsigned short dwg_face_get_edge_flags(HENTITY hEntity);
long dwg_face_set_edge_flags(HENTITY hEntity, unsigned short flags);

#ifdef __cplusplus
}
#endif

#endif
