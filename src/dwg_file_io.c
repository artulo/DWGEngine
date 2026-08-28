#include <stdio.h>
#include <stdlib.h>

#include "dwg_file_io.h"

unsigned char *dwg_read_whole_file(const char *path, unsigned long *out_length)
{
    FILE *fp;
    long size;
    unsigned char *buf;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }

    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL)
    {
        fclose(fp);
        return NULL;
    }

    if (fread(buf, 1, (size_t)size, fp) != (size_t)size)
    {
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    *out_length = (unsigned long)size;
    return buf;
}
