#include <stdlib.h>
#include <string.h>

#include "dwg_text.h"
#include "dwg_document.h"

static void dwg_text_copy_string(char *dst,
                                 unsigned long size,
                                 const char *src)
{
    unsigned long i;

    if (dst == NULL || size == 0UL)
        return;

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    for (i = 0UL; i + 1UL < size && src[i] != '\0'; ++i)
        dst[i] = src[i];

    dst[i] = '\0';
}

static DWG_TEXT *dwg_text_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_TEXT)
        return NULL;

    return (DWG_TEXT *)hEntity->geometry;
}

HENTITY dwg_add_text(HDWG hDwg,
                     double x, double y, double z,
                     double height,
                     double angle,
                     const char *text)
{
    DWG_TEXT *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_TEXT);
    if (entity == NULL)
        return NULL;

    data = (DWG_TEXT *)malloc(sizeof(DWG_TEXT));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    memset(data, 0, sizeof(DWG_TEXT));

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;

    data->point0.x = x;
    data->point0.y = y;
    data->point0.z = z;

    dwg_text_copy_string(data->text, DWG_TEXT_MAX, text);

    data->height = height;
    data->angle = angle;
    data->width_factor = 1.0;
    data->oblique = 0.0;
    data->align = DWG_TEXT_ALIGN_LEFT;

    entity->geometry = data;

    return entity;
}

const char *dwg_text_get_text(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->text;
}

long dwg_text_set_text(HENTITY hEntity, const char *text)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_text_copy_string(data->text, DWG_TEXT_MAX, text);

    return 1L;
}

const char *dwg_text_get_style_name(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->style_name;
}

long dwg_text_set_style_name(HENTITY hEntity, const char *style_name)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_text_copy_string(data->style_name, sizeof(data->style_name), style_name);

    return 1L;
}

void dwg_text_get_point(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->point.x;
    if (y != NULL) *y = data->point.y;
    if (z != NULL) *z = data->point.z;
}

void dwg_text_set_point(HENTITY hEntity, double x, double y, double z)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return;

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;
}

void dwg_text_get_point0(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->point0.x;
    if (y != NULL) *y = data->point0.y;
    if (z != NULL) *z = data->point0.z;
}

void dwg_text_set_point0(HENTITY hEntity, double x, double y, double z)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return;

    data->point0.x = x;
    data->point0.y = y;
    data->point0.z = z;
}

double dwg_text_get_height(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->height : 0.0;
}

long dwg_text_set_height(HENTITY hEntity, double height)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->height = height;

    return 1L;
}

double dwg_text_get_angle(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->angle : 0.0;
}

long dwg_text_set_angle(HENTITY hEntity, double angle)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->angle = angle;

    return 1L;
}

double dwg_text_get_width_factor(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->width_factor : 0.0;
}

long dwg_text_set_width_factor(HENTITY hEntity, double factor)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->width_factor = factor;

    return 1L;
}

double dwg_text_get_oblique(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->oblique : 0.0;
}

long dwg_text_set_oblique(HENTITY hEntity, double oblique)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->oblique = oblique;

    return 1L;
}

unsigned short dwg_text_get_align(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->align : 0;
}

long dwg_text_set_align(HENTITY hEntity, unsigned short align)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->align = align;

    return 1L;
}

DWG_BOOL dwg_text_get_backward(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->backward : DWG_FALSE;
}

long dwg_text_set_backward(HENTITY hEntity, DWG_BOOL value)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->backward = value;

    return 1L;
}

DWG_BOOL dwg_text_get_upside_down(HENTITY hEntity)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    return data != NULL ? data->upside_down : DWG_FALSE;
}

long dwg_text_set_upside_down(HENTITY hEntity, DWG_BOOL value)
{
    DWG_TEXT *data = dwg_text_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->upside_down = value;

    return 1L;
}
