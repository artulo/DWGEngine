#include <stdlib.h>
#include <string.h>

#include "dwg_style.h"

static void dwg_style_copy_text(char *dst,
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

HSTYLE dwg_style_create(const char *name)
{
    HSTYLE style;

    style = (HSTYLE)malloc(sizeof(DWG_STYLE));

    if (style == NULL)
        return NULL;

    memset(style, 0, sizeof(DWG_STYLE));

    dwg_style_copy_text(style->name, DWG_STYLE_NAME_MAX, name);

    style->height = DWG_STYLE_DEFAULT_HEIGHT;
    style->width_factor = DWG_STYLE_DEFAULT_WIDTH;
    style->oblique = 0.0;

    return style;
}

void dwg_style_destroy(HSTYLE style)
{
    if (style == NULL)
        return;

    free(style);
}

const char *dwg_style_get_name(HSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->name;
}

long dwg_style_set_name(HSTYLE style, const char *name)
{
    if (style == NULL)
        return 0L;

    dwg_style_copy_text(style->name, DWG_STYLE_NAME_MAX, name);

    return 1L;
}

const char *dwg_style_get_font(HSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->font_name;
}

long dwg_style_set_font(HSTYLE style, const char *font_name)
{
    if (style == NULL)
        return 0L;

    dwg_style_copy_text(style->font_name, DWG_STYLE_FONT_MAX, font_name);

    return 1L;
}

const char *dwg_style_get_ttf_name(HSTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->ttf_name;
}

long dwg_style_set_ttf_name(HSTYLE style, const char *ttf_name)
{
    if (style == NULL)
        return 0L;

    dwg_style_copy_text(style->ttf_name, DWG_STYLE_FONT_MAX, ttf_name);

    return 1L;
}

double dwg_style_get_height(HSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->height;
}

long dwg_style_set_height(HSTYLE style, double height)
{
    if (style == NULL)
        return 0L;

    style->height = height;

    return 1L;
}

double dwg_style_get_width_factor(HSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->width_factor;
}

long dwg_style_set_width_factor(HSTYLE style, double width_factor)
{
    if (style == NULL)
        return 0L;

    if (width_factor == 0.0)
    {
        style->width_factor = DWG_STYLE_DEFAULT_WIDTH;
        return 1L;
    }

    if (width_factor < DWG_STYLE_WIDTH_MIN || width_factor > DWG_STYLE_WIDTH_MAX)
        return 0L;

    style->width_factor = width_factor;

    return 1L;
}

double dwg_style_get_oblique(HSTYLE style)
{
    if (style == NULL)
        return 0.0;

    return style->oblique;
}

long dwg_style_set_oblique(HSTYLE style, double oblique)
{
    if (style == NULL)
        return 0L;

    if (oblique < -DWG_STYLE_OBLIQUE_MAX || oblique > DWG_STYLE_OBLIQUE_MAX)
        return 0L;

    style->oblique = oblique;

    return 1L;
}

DWG_BOOL dwg_style_get_backward(HSTYLE style)
{
    if (style == NULL)
        return DWG_FALSE;

    return style->backward;
}

long dwg_style_set_backward(HSTYLE style, DWG_BOOL value)
{
    if (style == NULL)
        return 0L;

    style->backward = value;

    return 1L;
}

DWG_BOOL dwg_style_get_upside_down(HSTYLE style)
{
    if (style == NULL)
        return DWG_FALSE;

    return style->upside_down;
}

long dwg_style_set_upside_down(HSTYLE style, DWG_BOOL value)
{
    if (style == NULL)
        return 0L;

    style->upside_down = value;

    return 1L;
}
