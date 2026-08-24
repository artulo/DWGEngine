#include <stdlib.h>
#include <string.h>

#include "dwg_dimstyle.h"

static void dwg_dimstyle_copy_text(char *dst,
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

HDIMSTYLE dwg_dimstyle_create(const char *name)
{
    HDIMSTYLE style;

    style = (HDIMSTYLE)malloc(sizeof(DWG_DIMSTYLE));

    if (style == NULL)
        return NULL;

    memset(style, 0, sizeof(DWG_DIMSTYLE));

    dwg_dimstyle_copy_text(style->name, DWG_DIMSTYLE_NAME_MAX, name);

    style->text_height = DWG_DIMSTYLE_DEFAULT_TEXT_HEIGHT;
    style->arrow_size = DWG_DIMSTYLE_DEFAULT_ARROW_SIZE;
    style->scale = DWG_DIMSTYLE_DEFAULT_SCALE;
    style->ext_offset = DWG_DIMSTYLE_DEFAULT_EXT_OFFSET;

    return style;
}

void dwg_dimstyle_destroy(HDIMSTYLE style)
{
    if (style == NULL)
        return;

    free(style);
}

const char *dwg_dimstyle_get_name(HDIMSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->name;
}

long dwg_dimstyle_set_name(HDIMSTYLE style, const char *name)
{
    if (style == NULL)
        return 0L;

    dwg_dimstyle_copy_text(style->name, DWG_DIMSTYLE_NAME_MAX, name);

    return 1L;
}

double dwg_dimstyle_get_text_height(HDIMSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->text_height;
}

long dwg_dimstyle_set_text_height(HDIMSTYLE style, double height)
{
    if (style == NULL)
        return 0L;

    style->text_height = height;

    return 1L;
}

double dwg_dimstyle_get_arrow_size(HDIMSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->arrow_size;
}

long dwg_dimstyle_set_arrow_size(HDIMSTYLE style, double size)
{
    if (style == NULL)
        return 0L;

    style->arrow_size = size;

    return 1L;
}

double dwg_dimstyle_get_scale(HDIMSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->scale;
}

long dwg_dimstyle_set_scale(HDIMSTYLE style, double scale)
{
    if (style == NULL)
        return 0L;

    style->scale = scale;

    return 1L;
}

double dwg_dimstyle_get_ext_offset(HDIMSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->ext_offset;
}

long dwg_dimstyle_set_ext_offset(HDIMSTYLE style, double offset)
{
    if (style == NULL)
        return 0L;

    style->ext_offset = offset;

    return 1L;
}

unsigned short dwg_dimstyle_get_precision(HDIMSTYLE style)
{
    if (style == NULL)
        return 0;

    return style->precision;
}

long dwg_dimstyle_set_precision(HDIMSTYLE style, unsigned short precision)
{
    if (style == NULL)
        return 0L;

    style->precision = precision;

    return 1L;
}
