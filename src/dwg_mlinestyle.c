#include <stdlib.h>
#include <string.h>

#include "dwg_mlinestyle.h"

static void dwg_mlinestyle_copy_text(char *dst,
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

HMLINESTYLE dwg_mlinestyle_create(const char *name)
{
    HMLINESTYLE style;

    style = (HMLINESTYLE)malloc(sizeof(DWG_MLINESTYLE));

    if (style == NULL)
        return NULL;

    memset(style, 0, sizeof(DWG_MLINESTYLE));

    dwg_mlinestyle_copy_text(style->name, DWG_MLINESTYLE_NAME_MAX, name);

    return style;
}

void dwg_mlinestyle_destroy(HMLINESTYLE style)
{
    if (style == NULL)
        return;

    free(style);
}

const char *dwg_mlinestyle_get_name(HMLINESTYLE style)
{
    if (style == NULL)
        return NULL;

    return style->name;
}

long dwg_mlinestyle_set_name(HMLINESTYLE style, const char *name)
{
    if (style == NULL)
        return 0L;

    dwg_mlinestyle_copy_text(style->name, DWG_MLINESTYLE_NAME_MAX, name);

    return 1L;
}

long dwg_mlinestyle_add_line(HMLINESTYLE style,
                             double offset,
                             unsigned short color,
                             const char *linetype_name)
{
    DWG_MLINE_ELEMENT *element;

    if (style == NULL)
        return 0L;

    if (style->line_count >= (unsigned long)DWG_MLINESTYLE_MAX_LINES)
        return 0L;

    element = &style->lines[style->line_count];

    element->offset = offset;
    element->color = color;
    dwg_mlinestyle_copy_text(element->linetype_name, sizeof(element->linetype_name), linetype_name);

    style->line_count++;

    return 1L;
}

unsigned long dwg_mlinestyle_line_count(HMLINESTYLE style)
{
    if (style == NULL)
        return 0UL;

    return style->line_count;
}

double dwg_mlinestyle_get_offset(HMLINESTYLE style, unsigned long index)
{
    if (style == NULL || index >= style->line_count)
        return 0.0;

    return style->lines[index].offset;
}

unsigned short dwg_mlinestyle_get_color(HMLINESTYLE style, unsigned long index)
{
    if (style == NULL || index >= style->line_count)
        return 0;

    return style->lines[index].color;
}

const char *dwg_mlinestyle_get_linetype(HMLINESTYLE style, unsigned long index)
{
    if (style == NULL || index >= style->line_count)
        return NULL;

    return style->lines[index].linetype_name;
}
