#include <stdlib.h>
#include <string.h>

#include "dwg_geometry.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_polyline.h"

static void *dwg_alloc_geometry(unsigned long size)
{
    void *p;

    p = malloc(size);
    if (p != NULL)
        memset(p, 0, size);

    return p;
}

HENTITY dwg_add_point(HDWG hDwg, double x, double y, double z)
{
    DWG_POINT3D *point;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_POINT);
    if (entity == NULL)
        return NULL;

    point = (DWG_POINT3D *)dwg_alloc_geometry(sizeof(DWG_POINT3D));
    if (point == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    point->x = x;
    point->y = y;
    point->z = z;
    entity->geometry = point;

    return entity;
}

HENTITY dwg_add_line(HDWG hDwg,
                     double x1, double y1, double z1,
                     double x2, double y2, double z2)
{
    DWG_LINE3D *line;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_LINE);
    if (entity == NULL)
        return NULL;

    line = (DWG_LINE3D *)dwg_alloc_geometry(sizeof(DWG_LINE3D));
    if (line == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    line->start.x = x1;
    line->start.y = y1;
    line->start.z = z1;

    line->end.x = x2;
    line->end.y = y2;
    line->end.z = z2;

    entity->geometry = line;

    return entity;
}

HENTITY dwg_add_circle(HDWG hDwg,
                       double x, double y, double z,
                       double radius)
{
    DWG_CIRCLE3D *circle;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_CIRCLE);
    if (entity == NULL)
        return NULL;

    circle = (DWG_CIRCLE3D *)dwg_alloc_geometry(sizeof(DWG_CIRCLE3D));
    if (circle == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    circle->center.x = x;
    circle->center.y = y;
    circle->center.z = z;
    circle->radius = radius;

    entity->geometry = circle;

    return entity;
}

HENTITY dwg_add_arc(HDWG hDwg,
                    double cx, double cy, double cz,
                    double radius,
                    double start_angle,
                    double end_angle)
{
    DWG_ARC3D *arc;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_ARC);
    if (entity == NULL)
        return NULL;

    arc = (DWG_ARC3D *)dwg_alloc_geometry(sizeof(DWG_ARC3D));
    if (arc == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    arc->center.x = cx;
    arc->center.y = cy;
    arc->center.z = cz;
    arc->radius = radius;
    arc->start_angle = start_angle;
    arc->end_angle = end_angle;

    entity->geometry = arc;

    return entity;
}

HENTITY dwg_add_polyline(HDWG hDwg)
{
    HENTITY entity;
    HPOLYLINE polyline;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_POLYLINE);
    if (entity == NULL)
        return NULL;

    polyline = (HPOLYLINE)dwg_alloc_geometry(sizeof(DWG_POLYLINE));
    if (polyline == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    polyline->entity = entity;
    entity->geometry = polyline;

    return entity;
}

HVERTEX dwg_add_vertex(HENTITY hPolyline,
                       double x, double y, double z)
{
    HPOLYLINE polyline;

    polyline = dwg_polyline_from_entity(hPolyline);
    if (polyline == NULL)
        return NULL;

    return dwg_polyline_add_vertex(polyline, x, y, z);
}

HVERTEX dwg_add_vertex2(HENTITY hPolyline,
                         double x, double y, double z,
                         double bulge,
                         double start_width,
                         double end_width)
{
    HPOLYLINE polyline;

    polyline = dwg_polyline_from_entity(hPolyline);
    if (polyline == NULL)
        return NULL;

    return dwg_polyline_add_vertex2(polyline,
                                    x, y, z,
                                    bulge,
                                    start_width,
                                    end_width);
}

static DWG_POINT3D *dwg_point_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_POINT)
        return NULL;

    return (DWG_POINT3D *)hEntity->geometry;
}

static DWG_LINE3D *dwg_line_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_LINE)
        return NULL;

    return (DWG_LINE3D *)hEntity->geometry;
}

static DWG_CIRCLE3D *dwg_circle_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_CIRCLE)
        return NULL;

    return (DWG_CIRCLE3D *)hEntity->geometry;
}

static DWG_ARC3D *dwg_arc_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_ARC)
        return NULL;

    return (DWG_ARC3D *)hEntity->geometry;
}

void dwg_point_get_xyz(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_POINT3D *data = dwg_point_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->x;
    if (y != NULL) *y = data->y;
    if (z != NULL) *z = data->z;
}

long dwg_point_set_xyz(HENTITY hEntity, double x, double y, double z)
{
    DWG_POINT3D *data = dwg_point_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->x = x;
    data->y = y;
    data->z = z;

    return 1L;
}

void dwg_line_get_start(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_LINE3D *data = dwg_line_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->start.x;
    if (y != NULL) *y = data->start.y;
    if (z != NULL) *z = data->start.z;
}

long dwg_line_set_start(HENTITY hEntity, double x, double y, double z)
{
    DWG_LINE3D *data = dwg_line_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->start.x = x;
    data->start.y = y;
    data->start.z = z;

    return 1L;
}

void dwg_line_get_end(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_LINE3D *data = dwg_line_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->end.x;
    if (y != NULL) *y = data->end.y;
    if (z != NULL) *z = data->end.z;
}

long dwg_line_set_end(HENTITY hEntity, double x, double y, double z)
{
    DWG_LINE3D *data = dwg_line_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->end.x = x;
    data->end.y = y;
    data->end.z = z;

    return 1L;
}

void dwg_circle_get_center(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_CIRCLE3D *data = dwg_circle_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->center.x;
    if (y != NULL) *y = data->center.y;
    if (z != NULL) *z = data->center.z;
}

long dwg_circle_set_center(HENTITY hEntity, double x, double y, double z)
{
    DWG_CIRCLE3D *data = dwg_circle_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->center.x = x;
    data->center.y = y;
    data->center.z = z;

    return 1L;
}

double dwg_circle_get_radius(HENTITY hEntity)
{
    DWG_CIRCLE3D *data = dwg_circle_from_entity(hEntity);

    if (data == NULL)
        return 0.0;

    return data->radius;
}

long dwg_circle_set_radius(HENTITY hEntity, double radius)
{
    DWG_CIRCLE3D *data = dwg_circle_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->radius = radius;

    return 1L;
}

void dwg_arc_get_center(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->center.x;
    if (y != NULL) *y = data->center.y;
    if (z != NULL) *z = data->center.z;
}

long dwg_arc_set_center(HENTITY hEntity, double x, double y, double z)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->center.x = x;
    data->center.y = y;
    data->center.z = z;

    return 1L;
}

double dwg_arc_get_radius(HENTITY hEntity)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return 0.0;

    return data->radius;
}

long dwg_arc_set_radius(HENTITY hEntity, double radius)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->radius = radius;

    return 1L;
}

void dwg_arc_get_angles(HENTITY hEntity, double *start_angle, double *end_angle)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return;

    if (start_angle != NULL) *start_angle = data->start_angle;
    if (end_angle != NULL) *end_angle = data->end_angle;
}

long dwg_arc_set_angles(HENTITY hEntity, double start_angle, double end_angle)
{
    DWG_ARC3D *data = dwg_arc_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->start_angle = start_angle;
    data->end_angle = end_angle;

    return 1L;
}
