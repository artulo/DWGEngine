#ifndef DWG_PAGE_H
#define DWG_PAGE_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_PAGE_NAME_MAX 64

#define DWG_PAGE_PORTRAIT  0
#define DWG_PAGE_LANDSCAPE 1

/*
 * Defaults match real vecad.dll's CPage constructor, recovered from
 * D:\estudio\DWGEngine\reverse\CPage_notes.md: 210x297mm (ISO A4),
 * 100% scale on both axes.
 */
#define DWG_PAGE_DEFAULT_WIDTH   210.0
#define DWG_PAGE_DEFAULT_HEIGHT  297.0
#define DWG_PAGE_DEFAULT_SCALE   100.0

typedef struct _DWG_PAGE DWG_PAGE;
typedef DWG_PAGE * HPAGE;

struct _DWG_PAGE
{
    char name[DWG_PAGE_NAME_MAX];

    unsigned char orientation;
    unsigned short paper_size_id;

    double width;   /* millimeters */
    double height;  /* millimeters */

    double scale_x; /* percent */
    double scale_y; /* percent */

    HPAGE next;
    HPAGE prev;
};

HPAGE dwg_page_create(const char *name);
void dwg_page_destroy(HPAGE page);

const char *dwg_page_get_name(HPAGE page);
long dwg_page_set_name(HPAGE page, const char *name);

unsigned char dwg_page_get_orientation(HPAGE page);
long dwg_page_set_orientation(HPAGE page, unsigned char orientation);

unsigned short dwg_page_get_size_id(HPAGE page);
long dwg_page_set_size_id(HPAGE page, unsigned short size_id);

double dwg_page_get_width(HPAGE page);
long dwg_page_set_width(HPAGE page, double width);

double dwg_page_get_height(HPAGE page);
long dwg_page_set_height(HPAGE page, double height);

double dwg_page_get_scale_x(HPAGE page);
long dwg_page_set_scale_x(HPAGE page, double scale);

double dwg_page_get_scale_y(HPAGE page);
long dwg_page_set_scale_y(HPAGE page, double scale);

#ifdef __cplusplus
}
#endif

#endif
