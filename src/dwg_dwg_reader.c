#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dwg_file_io.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_layer.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_style.h"
#include "dwg_linetype.h"

/*
 * AutoCAD R12 (AC1009) binary reader. Validated directly against a real
 * R11 (same AC1009 format) sample file from LibreDWG's public test data
 * (reverse/samples/r11_entities-2d.dwg) -- see
 * reverse/DWG_R12_format_reference.md for how that sample was obtained
 * and what it confirmed. Handles the entity kinds this engine models:
 * LINE, POINT, CIRCLE, ARC, PLINE/VERTEX. Unknown kinds are skipped via
 * their self-framing 'length' field, same mechanism the writer relies on.
 */

#define DWG_KIND_LINE    1
#define DWG_KIND_POINT   2
#define DWG_KIND_CIRCLE  3
#define DWG_KIND_ARC     8
#define DWG_KIND_PLINE   19
#define DWG_KIND_VERTEX  20
#define DWG_KIND_TEXT    7
#define DWG_KIND_SOLID   11
#define DWG_KIND_INSERT  14

static unsigned short rd_word(const unsigned char *p)
{
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned long rd_long(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static double rd_double(const unsigned char *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

typedef struct
{
    char *names; /* count * 64 bytes, one name per slot */
    unsigned long count;
} DWG_LAYER_INDEX;

/* Shared by the LAYER (record_size 41) and BLOCK (record_size 45) tables
   -- both put byte:flag + char[32]:name at the very start of each
   record, just with different trailing fields/sizes. */
static void read_name_table(FILE *fp, unsigned long start, unsigned long nr,
                            unsigned long record_size, DWG_LAYER_INDEX *index)
{
    unsigned long i;
    unsigned char rec[64];

    index->names = NULL;
    index->count = 0UL;

    if (nr == 0UL || record_size > sizeof(rec))
        return;

    index->names = (char *)malloc((size_t)nr * 64U);
    if (index->names == NULL)
        return;

    if (fseek(fp, (long)start, SEEK_SET) != 0)
        return;

    for (i = 0UL; i < nr; i++)
    {
        if (fread(rec, 1, (size_t)record_size, fp) != (size_t)record_size)
            break;

        memset(index->names + i * 64U, 0, 64U);
        memcpy(index->names + i * 64U, rec + 1, 32); /* name is char[32] right after the flag byte */
    }

    index->count = i;
}

static const char *layer_name_for_index(const DWG_LAYER_INDEX *index, unsigned short idx)
{
    if (index->names == NULL || idx >= index->count)
        return NULL;

    return index->names + (unsigned long)idx * 64U;
}

/* Reads the common entity header already positioned right after the
   4-byte kind+flag+length; returns bytes consumed from *p, advances *p.
   layer/opts/color/elevation are filled in; has_elevation mirrors the
   writer's own flag&4 convention. */
static void read_common(const unsigned char **p,
                        unsigned char flag,
                        unsigned short *layer_idx,
                        unsigned short *opts,
                        unsigned char *color,
                        double *elevation,
                        int *has_elevation)
{
    *layer_idx = rd_word(*p); *p += 2;
    *opts = rd_word(*p); *p += 2;

    *color = 0;
    if (flag & 0x01U)
    {
        *color = **p; *p += 1;
    }

    *has_elevation = (flag & 0x04U) ? 1 : 0;
    *elevation = 0.0;
    if (*has_elevation)
    {
        *elevation = rd_double(*p); *p += 8;
    }
}

static void apply_layer_color(HENTITY entity, const DWG_LAYER_INDEX *index,
                              unsigned short layer_idx, unsigned char color)
{
    const char *name = layer_name_for_index(index, layer_idx);

    if (name != NULL)
        dwg_entity_put_layer(entity, name);

    if (color != 0)
        dwg_entity_put_color(entity, color);
}

HDWG dwg_read_dwg_r12(const char *path, DWG_IO_RESULT *result)
{
    FILE *fp;
    unsigned char hdr[94];
    HDWG hDwg;
    unsigned long p_entities, p_entend;
    unsigned long layer_start, layer_nr;
    unsigned long block_start, block_nr;
    unsigned long style_start, style_nr;
    unsigned long ltype_start, ltype_nr;
    DWG_LAYER_INDEX layer_index;
    DWG_LAYER_INDEX block_index;
    unsigned char *entities_buf;
    unsigned long entities_len;
    const unsigned char *p, *end;

    if (result != NULL)
        *result = DWG_IO_OK;

    if (path == NULL)
    {
        if (result != NULL) *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        if (result != NULL) *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    if (fread(hdr, 1, 94, fp) != 94)
    {
        fclose(fp);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    if (memcmp(hdr, "AC1009", 6) != 0)
    {
        fclose(fp);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    p_entities = rd_long(hdr + 20);
    p_entend   = rd_long(hdr + 24);
    block_nr    = rd_long(hdr + 46);
    block_start = rd_long(hdr + 50);
    layer_nr    = rd_long(hdr + 56);
    layer_start = rd_long(hdr + 60);
    style_nr    = rd_long(hdr + 66);
    style_start = rd_long(hdr + 70);
    ltype_nr    = rd_long(hdr + 76);
    ltype_start = rd_long(hdr + 80);

    read_name_table(fp, layer_start, layer_nr, 41UL, &layer_index);
    read_name_table(fp, block_start, block_nr, 45UL, &block_index);

    hDwg = dwg_document_create();
    if (hDwg == NULL)
    {
        fclose(fp);
        if (layer_index.names != NULL) free(layer_index.names);
        if (block_index.names != NULL) free(block_index.names);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    {
        HLAYER layer;
        unsigned long i;
        char namebuf[65];

        for (i = 0UL; i < layer_index.count; i++)
        {
            memcpy(namebuf, layer_index.names + i * 64U, 64);
            namebuf[64] = '\0';
            if (namebuf[0] != '\0')
            {
                layer = dwg_document_add_layer(hDwg, namebuf);
                (void)layer;
            }
        }
    }

    {
        HBLOCK block;
        unsigned long i;
        char namebuf[65];

        for (i = 0UL; i < block_index.count; i++)
        {
            memcpy(namebuf, block_index.names + i * 64U, 64);
            namebuf[64] = '\0';
            if (namebuf[0] != '\0')
            {
                block = dwg_document_add_block(hDwg, namebuf);
                (void)block;
            }
        }
    }

    /* STYLE (198-byte records) and LTYPE (191-byte records): field
       offsets confirmed from a real sample -- see the STYLE_RECORD_SIZE
       comment in dwg_dwg_writer.c for the full sourcing. Read directly
       here rather than through read_name_table since these tables carry
       more than just a name. */
    if (style_nr > 0UL && fseek(fp, (long)style_start, SEEK_SET) == 0)
    {
        unsigned char rec[198];
        unsigned long i;

        for (i = 0UL; i < style_nr; i++)
        {
            char namebuf[33];
            char fontbuf[129];
            HSTYLE style;

            if (fread(rec, 1, sizeof(rec), fp) != sizeof(rec))
                break;

            memcpy(namebuf, rec + 1, 32); namebuf[32] = '\0';
            if (namebuf[0] == '\0')
                continue;

            style = dwg_document_add_style(hDwg, namebuf);
            if (style != NULL)
            {
                dwg_style_set_height(style, rd_double(rec + 35));
                dwg_style_set_width_factor(style, rd_double(rec + 43));
                dwg_style_set_oblique(style, rd_double(rec + 51));
                memcpy(fontbuf, rec + 68, 128); fontbuf[128] = '\0';
                dwg_style_set_font(style, fontbuf);
            }
        }
    }

    if (ltype_nr > 0UL && fseek(fp, (long)ltype_start, SEEK_SET) == 0)
    {
        unsigned char rec[191];
        unsigned long i;

        for (i = 0UL; i < ltype_nr; i++)
        {
            char namebuf[33];
            char descbuf[49];
            HLINETYPE linetype;

            if (fread(rec, 1, sizeof(rec), fp) != sizeof(rec))
                break;

            memcpy(namebuf, rec + 1, 32); namebuf[32] = '\0';
            if (namebuf[0] == '\0')
                continue;

            linetype = dwg_document_add_linetype(hDwg, namebuf);
            if (linetype != NULL)
            {
                memcpy(descbuf, rec + 35, 48); descbuf[48] = '\0';
                dwg_linetype_set_descr(linetype, descbuf);
            }
        }
    }

    if (p_entend < p_entities)
    {
        fclose(fp);
        if (layer_index.names != NULL) free(layer_index.names);
        if (block_index.names != NULL) free(block_index.names);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return hDwg;
    }

    entities_len = p_entend - p_entities;
    entities_buf = (unsigned char *)malloc((size_t)entities_len);
    if (entities_buf == NULL)
    {
        fclose(fp);
        if (layer_index.names != NULL) free(layer_index.names);
        if (block_index.names != NULL) free(block_index.names);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return hDwg;
    }

    if (fseek(fp, (long)p_entities, SEEK_SET) != 0 ||
        fread(entities_buf, 1, (size_t)entities_len, fp) != (size_t)entities_len)
    {
        free(entities_buf);
        fclose(fp);
        if (layer_index.names != NULL) free(layer_index.names);
        if (block_index.names != NULL) free(block_index.names);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return hDwg;
    }

    fclose(fp);

    p = entities_buf;
    end = entities_buf + entities_len;

    while (p + 4 <= end)
    {
        unsigned char kind = p[0];
        unsigned char flag = p[1];
        unsigned short length = rd_word(p + 2);
        const unsigned char *entity_start = p;
        const unsigned char *body;
        unsigned short layer_idx, opts;
        unsigned char color;
        double elevation;
        int has_elevation;

        if (length < 4 || entity_start + length > end)
            break; /* malformed / truncated -- stop rather than read out of bounds */

        body = p + 4;
        read_common(&body, flag, &layer_idx, &opts, &color, &elevation, &has_elevation);

        switch (kind)
        {
        case DWG_KIND_LINE:
        {
            double x1 = rd_double(body); double y1 = rd_double(body + 8); double z1 = rd_double(body + 16);
            double x2 = rd_double(body + 24); double y2 = rd_double(body + 32); double z2 = rd_double(body + 40);
            HENTITY e = dwg_add_line(hDwg, x1, y1, z1, x2, y2, z2);
            if (e != NULL) apply_layer_color(e, &layer_index, layer_idx, color);
            break;
        }
        case DWG_KIND_POINT:
        {
            double x = rd_double(body); double y = rd_double(body + 8); double z = rd_double(body + 16);
            HENTITY e = dwg_add_point(hDwg, x, y, z);
            if (e != NULL) apply_layer_color(e, &layer_index, layer_idx, color);
            break;
        }
        case DWG_KIND_CIRCLE:
        {
            double x = rd_double(body); double y = rd_double(body + 8);
            double radius = rd_double(body + 16);
            HENTITY e = dwg_add_circle(hDwg, x, y, has_elevation ? elevation : 0.0, radius);
            if (e != NULL) apply_layer_color(e, &layer_index, layer_idx, color);
            break;
        }
        case DWG_KIND_ARC:
        {
            double x = rd_double(body); double y = rd_double(body + 8);
            double radius = rd_double(body + 16);
            double start_angle = rd_double(body + 24);
            double end_angle = rd_double(body + 32);
            HENTITY e = dwg_add_arc(hDwg, x, y, has_elevation ? elevation : 0.0,
                                    radius, start_angle, end_angle);
            if (e != NULL) apply_layer_color(e, &layer_index, layer_idx, color);
            break;
        }
        case DWG_KIND_TEXT:
        {
            double x = rd_double(body); double y = rd_double(body + 8);
            double height = rd_double(body + 16);
            unsigned short slen = rd_word(body + 24);
            char textbuf[DWG_TEXT_MAX];
            unsigned long off = 26UL;
            double x0 = x, y0 = y;
            HENTITY e;

            if (slen >= DWG_TEXT_MAX) slen = DWG_TEXT_MAX - 1U;
            memcpy(textbuf, body + 24 + 2, slen);
            textbuf[slen] = '\0';
            off += slen;

            e = dwg_add_text(hDwg, x, y, has_elevation ? elevation : 0.0,
                             height, 0.0, textbuf);

            if (opts & 0x0020U) { off += 1UL; } /* l72 (alignment byte), skipped for now */
            if (opts & 0x0040U)
            {
                x0 = rd_double(body + off); off += 8UL;
                y0 = rd_double(body + off); off += 8UL;
            }

            if (e != NULL)
            {
                dwg_text_set_point0(e, x0, y0, has_elevation ? elevation : 0.0);
                apply_layer_color(e, &layer_index, layer_idx, color);
            }
            break;
        }
        case DWG_KIND_INSERT:
        {
            unsigned short block_idx = rd_word(body);
            double x = rd_double(body + 2), y = rd_double(body + 10);
            unsigned long off = 18UL;
            double sx = 1.0, sy = 1.0, sz = 1.0, angle = 0.0;
            const char *block_name;
            HENTITY e;

            /* Field order/labels (l41/l42/l43=scale x/y/z, l50=angle)
               follow standard DXF INSERT group-code conventions; the
               real sample confirmed the VALUES (0.5, 0.5, and an exact
               30-degree-in-radians constant) but not definitively which
               of l43/l50 is which, since only one real INSERT was
               available to check -- see DWG_R12_format_reference.md. */
            if (opts & 0x0001U) { sx = rd_double(body + off); off += 8UL; }
            if (opts & 0x0002U) { sy = rd_double(body + off); off += 8UL; }
            if (opts & 0x0004U) { sz = rd_double(body + off); off += 8UL; }
            if (opts & 0x0008U) { angle = rd_double(body + off); off += 8UL; }

            block_name = layer_name_for_index(&block_index, block_idx);

            e = dwg_add_insert(hDwg, block_name != NULL ? block_name : "",
                               x, y, has_elevation ? elevation : 0.0, angle);
            if (e != NULL)
            {
                dwg_insert_set_scale(e, sx, sy, sz);
                apply_layer_color(e, &layer_index, layer_idx, color);
            }
            break;
        }
        case DWG_KIND_SOLID:
        {
            double px1 = rd_double(body), py1 = rd_double(body + 8);
            double px2 = rd_double(body + 16), py2 = rd_double(body + 24);
            double px3 = rd_double(body + 32), py3 = rd_double(body + 40);
            double px4 = rd_double(body + 48), py4 = rd_double(body + 56);
            double z = has_elevation ? elevation : 0.0;
            HENTITY e = dwg_add_solid(hDwg, px1, py1, z, px2, py2, z, px3, py3, z, px4, py4, z);
            if (e != NULL) apply_layer_color(e, &layer_index, layer_idx, color);
            break;
        }
        case DWG_KIND_PLINE:
        {
            HENTITY e = dwg_add_polyline(hDwg);
            HPOLYLINE pl = dwg_polyline_from_entity(e);
            long pflags = 0;

            if (opts & 0x0001U)
            {
                pflags = *body; /* byte l70 */
            }

            if (pl != NULL)
            {
                dwg_polyline_set_closed(pl, pflags & 1L);
                if (has_elevation)
                    dwg_polyline_set_elevation(pl, elevation);
            }

            if (e != NULL)
                apply_layer_color(e, &layer_index, layer_idx, color);

            /* Consume following VERTEX (kind 20) entities into this
               polyline until a non-VERTEX kind appears -- same
               convention the writer uses (no explicit terminator). */
            {
                const unsigned char *vp = entity_start + length;

                while (vp + 4 <= end && vp[0] == DWG_KIND_VERTEX)
                {
                    unsigned char vflag = vp[1];
                    unsigned short vlength = rd_word(vp + 2);
                    const unsigned char *vbody = vp + 4;
                    unsigned short vlayer_idx, vopts;
                    unsigned char vcolor;
                    double velevation;
                    int vhas_elevation;

                    if (vlength < 4 || vp + vlength > end)
                        break;

                    read_common(&vbody, vflag, &vlayer_idx, &vopts, &vcolor, &velevation, &vhas_elevation);

                    {
                        double vx = rd_double(vbody);
                        double vy = rd_double(vbody + 8);
                        double bulge = 0.0;
                        unsigned long off = 16UL;

                        if (vopts & 0x0001U) { off += 8UL; } /* l40, skipped */
                        if (vopts & 0x0002U) { off += 8UL; } /* l41, skipped */
                        if (vopts & 0x0004U) { off += 1UL; } /* l70 flags, skipped */
                        if (vopts & 0x0008U) { bulge = rd_double(vbody + off); }

                        if (pl != NULL)
                            dwg_polyline_add_vertex2(pl, vx, vy, vhas_elevation ? velevation : 0.0,
                                                     bulge, 0.0, 0.0);
                    }

                    vp += vlength;
                }

                /* advance the OUTER loop past every vertex we consumed */
                length = (unsigned short)(vp - entity_start);
            }
            break;
        }
        default:
            break; /* unmodeled kind: skip via length, don't fail the whole read */
        }

        p = entity_start + length;
    }

    free(entities_buf);
    if (layer_index.names != NULL)
        free(layer_index.names);
    if (block_index.names != NULL)
        free(block_index.names);

    return hDwg;
}
