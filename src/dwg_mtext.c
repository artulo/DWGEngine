#include <stdlib.h>
#include <string.h>

#include "dwg_mtext.h"
#include "dwg_document.h"

static void dwg_mtext_copy_string(char *dst,
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

static DWG_MTEXT *dwg_mtext_from_entity(HENTITY hEntity)
{
    if (hEntity == NULL)
        return NULL;

    if (hEntity->type != DWG_ENTITY_MTEXT)
        return NULL;

    return (DWG_MTEXT *)hEntity->geometry;
}

HENTITY dwg_add_mtext(HDWG hDwg,
                      double x, double y, double z,
                      double height,
                      double rect_width,
                      const char *text)
{
    DWG_MTEXT *data;
    HENTITY entity;

    if (hDwg == NULL)
        return NULL;

    entity = dwg_document_add_entity(hDwg, DWG_ENTITY_MTEXT);
    if (entity == NULL)
        return NULL;

    data = (DWG_MTEXT *)malloc(sizeof(DWG_MTEXT));
    if (data == NULL)
    {
        dwg_document_remove_entity(hDwg, entity);
        return NULL;
    }

    memset(data, 0, sizeof(DWG_MTEXT));

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;

    dwg_mtext_copy_string(data->text, DWG_MTEXT_MAX, text);

    data->height = height;
    data->rect_width = rect_width;
    data->angle = 0.0;
    data->line_space = 1.0;
    data->attach = DWG_MTEXT_ATTACH_TOP_LEFT;

    entity->geometry = data;

    return entity;
}

const char *dwg_mtext_get_text(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->text;
}

long dwg_mtext_set_text(HENTITY hEntity, const char *text)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_mtext_copy_string(data->text, DWG_MTEXT_MAX, text);

    return 1L;
}

const char *dwg_mtext_get_style_name(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return NULL;

    return data->style_name;
}

long dwg_mtext_set_style_name(HENTITY hEntity, const char *style_name)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    dwg_mtext_copy_string(data->style_name, sizeof(data->style_name), style_name);

    return 1L;
}

void dwg_mtext_get_point(HENTITY hEntity, double *x, double *y, double *z)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return;

    if (x != NULL) *x = data->point.x;
    if (y != NULL) *y = data->point.y;
    if (z != NULL) *z = data->point.z;
}

void dwg_mtext_set_point(HENTITY hEntity, double x, double y, double z)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return;

    data->point.x = x;
    data->point.y = y;
    data->point.z = z;
}

double dwg_mtext_get_height(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    return data != NULL ? data->height : 0.0;
}

long dwg_mtext_set_height(HENTITY hEntity, double height)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->height = height;

    return 1L;
}

double dwg_mtext_get_rect_width(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    return data != NULL ? data->rect_width : 0.0;
}

long dwg_mtext_set_rect_width(HENTITY hEntity, double rect_width)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->rect_width = rect_width;

    return 1L;
}

double dwg_mtext_get_angle(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    return data != NULL ? data->angle : 0.0;
}

long dwg_mtext_set_angle(HENTITY hEntity, double angle)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->angle = angle;

    return 1L;
}

double dwg_mtext_get_line_space(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    return data != NULL ? data->line_space : 0.0;
}

long dwg_mtext_set_line_space(HENTITY hEntity, double line_space)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->line_space = line_space;

    return 1L;
}

unsigned short dwg_mtext_get_attach(HENTITY hEntity)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    return data != NULL ? data->attach : 0;
}

long dwg_mtext_set_attach(HENTITY hEntity, unsigned short attach)
{
    DWG_MTEXT *data = dwg_mtext_from_entity(hEntity);

    if (data == NULL)
        return 0L;

    data->attach = attach;

    return 1L;
}
