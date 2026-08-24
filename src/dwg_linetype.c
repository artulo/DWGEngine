#include <stdlib.h>
#include <string.h>

#include "dwg_linetype.h"

static void dwg_linetype_copy_text(char *dst,
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

HLINETYPE dwg_linetype_create(const char *name)
{
    HLINETYPE linetype;

    linetype = (HLINETYPE)malloc(sizeof(DWG_LINETYPE));

    if (linetype == NULL)
        return NULL;

    memset(linetype, 0, sizeof(DWG_LINETYPE));

    dwg_linetype_copy_text(linetype->name, DWG_LINETYPE_NAME_MAX, name);

    return linetype;
}

void dwg_linetype_destroy(HLINETYPE linetype)
{
    if (linetype == NULL)
        return;

    free(linetype);
}

const char *dwg_linetype_get_name(HLINETYPE linetype)
{
    if (linetype == NULL)
        return NULL;

    return linetype->name;
}

long dwg_linetype_set_name(HLINETYPE linetype, const char *name)
{
    if (linetype == NULL)
        return 0L;

    dwg_linetype_copy_text(linetype->name, DWG_LINETYPE_NAME_MAX, name);

    return 1L;
}

const char *dwg_linetype_get_descr(HLINETYPE linetype)
{
    if (linetype == NULL)
        return NULL;

    return linetype->description;
}

long dwg_linetype_set_descr(HLINETYPE linetype, const char *descr)
{
    if (linetype == NULL)
        return 0L;

    dwg_linetype_copy_text(linetype->description, DWG_LINETYPE_DESCR_MAX, descr);

    return 1L;
}
