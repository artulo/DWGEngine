#include <stdlib.h>
#include <string.h>

#include "dwg_pointstyle.h"

static void dwg_pointstyle_copy_text(char *dst,
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

HPOINTSTYLE dwg_pointstyle_create(const char *name)
{
    HPOINTSTYLE style;

    style = (HPOINTSTYLE)malloc(sizeof(DWG_POINTSTYLE));

    if (style == NULL)
        return NULL;

    memset(style, 0, sizeof(DWG_POINTSTYLE));

    dwg_pointstyle_copy_text(style->name, DWG_POINTSTYLE_NAME_MAX, name);

    style->block_scale = DWG_POINTSTYLE_DEFAULT_BLOCK_SCALE;
    style->text_height = DWG_POINTSTYLE_DEFAULT_TEXT_HEIGHT;
    style->text_width = DWG_POINTSTYLE_DEFAULT_TEXT_WIDTH;
    style->snap = DWG_POINTSTYLE_DEFAULT_SNAP;

    return style;
}

void dwg_pointstyle_destroy(HPOINTSTYLE style)
{
    if (style == NULL)
        return;

    free(style);
}

const char *dwg_pointstyle_get_name(HPOINTSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->name;
}

long dwg_pointstyle_set_name(HPOINTSTYLE style, const char *name)
{
    if (style == NULL)
        return 0L;

    dwg_pointstyle_copy_text(style->name, DWG_POINTSTYLE_NAME_MAX, name);

    return 1L;
}

long dwg_pointstyle_get_block_id(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0L;

    return style->block_id;
}

long dwg_pointstyle_set_block_id(HPOINTSTYLE style, long block_id)
{
    if (style == NULL)
        return 0L;

    style->block_id = block_id;

    return 1L;
}

double dwg_pointstyle_get_block_scale(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->block_scale;
}

long dwg_pointstyle_set_block_scale(HPOINTSTYLE style, double scale)
{
    if (style == NULL)
        return 0L;

    if (scale <= DWG_POINTSTYLE_BLOCK_SCALE_MIN || scale > DWG_POINTSTYLE_BLOCK_SCALE_MAX)
        return 0L;

    style->block_scale = scale;

    return 1L;
}

long dwg_pointstyle_get_font(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0L;

    return style->font;
}

long dwg_pointstyle_set_font(HPOINTSTYLE style, long font)
{
    if (style == NULL)
        return 0L;

    style->font = font;

    return 1L;
}

DWG_BOOL dwg_pointstyle_get_fixed(HPOINTSTYLE style)
{
    if (style == NULL)
        return DWG_FALSE;

    return style->fixed;
}

long dwg_pointstyle_set_fixed(HPOINTSTYLE style, DWG_BOOL value)
{
    if (style == NULL)
        return 0L;

    style->fixed = value;

    return 1L;
}

double dwg_pointstyle_get_text_height(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->text_height;
}

long dwg_pointstyle_set_text_height(HPOINTSTYLE style, double height)
{
    if (style == NULL)
        return 0L;

    if (height < DWG_POINTSTYLE_TEXT_HEIGHT_MIN || height >= DWG_POINTSTYLE_TEXT_HEIGHT_MAX)
        return 0L;

    style->text_height = height;

    return 1L;
}

double dwg_pointstyle_get_text_width(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->text_width;
}

long dwg_pointstyle_set_text_width(HPOINTSTYLE style, double width)
{
    if (style == NULL)
        return 0L;

    if (width < DWG_POINTSTYLE_TEXT_WIDTH_MIN || width > DWG_POINTSTYLE_TEXT_WIDTH_MAX)
        return 0L;

    style->text_width = width;

    return 1L;
}

unsigned char dwg_pointstyle_get_snap(HPOINTSTYLE style)
{
    if (style == NULL)
        return 0;

    return style->snap;
}

long dwg_pointstyle_set_snap(HPOINTSTYLE style, unsigned char snap)
{
    if (style == NULL)
        return 0L;

    style->snap = snap;

    return 1L;
}
