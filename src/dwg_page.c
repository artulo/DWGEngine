#include <stdlib.h>
#include <string.h>

#include "dwg_page.h"

static void dwg_page_copy_text(char *dst,
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

HPAGE dwg_page_create(const char *name)
{
    HPAGE page;

    page = (HPAGE)malloc(sizeof(DWG_PAGE));

    if (page == NULL)
        return NULL;

    memset(page, 0, sizeof(DWG_PAGE));

    dwg_page_copy_text(page->name, DWG_PAGE_NAME_MAX, name);

    page->orientation = DWG_PAGE_PORTRAIT;
    page->paper_size_id = 0;

    page->width = DWG_PAGE_DEFAULT_WIDTH;
    page->height = DWG_PAGE_DEFAULT_HEIGHT;

    page->scale_x = DWG_PAGE_DEFAULT_SCALE;
    page->scale_y = DWG_PAGE_DEFAULT_SCALE;

    page->next = NULL;
    page->prev = NULL;

    return page;
}

void dwg_page_destroy(HPAGE page)
{
    if (page == NULL)
        return;

    free(page);
}

const char *dwg_page_get_name(HPAGE page)
{
    if (page == NULL)
        return NULL;

    return page->name;
}

long dwg_page_set_name(HPAGE page, const char *name)
{
    if (page == NULL)
        return 0L;

    dwg_page_copy_text(page->name, DWG_PAGE_NAME_MAX, name);

    return 1L;
}

unsigned char dwg_page_get_orientation(HPAGE page)
{
    if (page == NULL)
        return DWG_PAGE_PORTRAIT;

    return page->orientation;
}

long dwg_page_set_orientation(HPAGE page, unsigned char orientation)
{
    if (page == NULL)
        return 0L;

    page->orientation = orientation;

    return 1L;
}

unsigned short dwg_page_get_size_id(HPAGE page)
{
    if (page == NULL)
        return 0;

    return page->paper_size_id;
}

long dwg_page_set_size_id(HPAGE page, unsigned short size_id)
{
    if (page == NULL)
        return 0L;

    page->paper_size_id = size_id;

    return 1L;
}

double dwg_page_get_width(HPAGE page)
{
    if (page == NULL)
        return 0.0;

    return page->width;
}

long dwg_page_set_width(HPAGE page, double width)
{
    if (page == NULL)
        return 0L;

    page->width = width;

    return 1L;
}

double dwg_page_get_height(HPAGE page)
{
    if (page == NULL)
        return 0.0;

    return page->height;
}

long dwg_page_set_height(HPAGE page, double height)
{
    if (page == NULL)
        return 0L;

    page->height = height;

    return 1L;
}

double dwg_page_get_scale_x(HPAGE page)
{
    if (page == NULL)
        return 0.0;

    return page->scale_x;
}

long dwg_page_set_scale_x(HPAGE page, double scale)
{
    if (page == NULL)
        return 0L;

    page->scale_x = scale;

    return 1L;
}

double dwg_page_get_scale_y(HPAGE page)
{
    if (page == NULL)
        return 0.0;

    return page->scale_y;
}

long dwg_page_set_scale_y(HPAGE page, double scale)
{
    if (page == NULL)
        return 0L;

    page->scale_y = scale;

    return 1L;
}
