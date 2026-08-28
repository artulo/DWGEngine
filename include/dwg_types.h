#ifndef DWG_TYPES_H
#define DWG_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DWG_DOCUMENT DWG_DOCUMENT;
typedef struct _DWG_ENTITY DWG_ENTITY;
typedef struct _DWG_VERTEX DWG_VERTEX;
typedef struct _DWG_POLYLINE DWG_POLYLINE;

typedef DWG_DOCUMENT * HDWG;
typedef DWG_ENTITY * HENTITY;
typedef DWG_VERTEX * HVERTEX;
typedef DWG_POLYLINE * HPOLYLINE;

typedef unsigned long DWG_ID;
typedef unsigned long DWG_FLAGS;
typedef long DWG_BOOL;

#define DWG_TRUE  1L
#define DWG_FALSE 0L

typedef enum
{
    DWG_ENTITY_UNKNOWN = 0,
    DWG_ENTITY_POINT,
    DWG_ENTITY_LINE,
    DWG_ENTITY_CIRCLE,
    DWG_ENTITY_ARC,
    DWG_ENTITY_POLYLINE,
    DWG_ENTITY_VERTEX,
    DWG_ENTITY_TEXT,
    DWG_ENTITY_MTEXT,
    DWG_ENTITY_ELLIPSE,
    DWG_ENTITY_RAY,
    DWG_ENTITY_XLINE,
    DWG_ENTITY_INSERT,
    DWG_ENTITY_BLOCK,
    DWG_ENTITY_HATCH,
    DWG_ENTITY_DIMENSION,
    DWG_ENTITY_SOLID,
    DWG_ENTITY_FACE,
    DWG_ENTITY_LEADER
} DWG_ENTITY_TYPE;

typedef struct _DWG_POINT3D
{
    double x;
    double y;
    double z;
} DWG_POINT3D;

typedef struct _DWG_LINE3D
{
    DWG_POINT3D start;
    DWG_POINT3D end;
} DWG_LINE3D;

typedef struct _DWG_CIRCLE3D
{
    DWG_POINT3D center;
    double radius;
} DWG_CIRCLE3D;

typedef struct _DWG_ARC3D
{
    DWG_POINT3D center;
    double radius;
    double start_angle;
    double end_angle;
} DWG_ARC3D;

typedef struct _DWG_ELLIPSE3D
{
    DWG_POINT3D center;
    DWG_POINT3D major_axis_endpoint;
    double axis_ratio;
    double start_param;
    double end_param;
} DWG_ELLIPSE3D;

#ifdef __cplusplus
}
#endif

#endif
