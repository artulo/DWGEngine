#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dwg_r1314_reader.h"
#include "dwg_r2000_reader.h"
#include "dwg_bitstream.h"
#include "dwg_document.h"
#include "dwg_geometry.h"
#include "dwg_text.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_transform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Object type codes -- same fixed values as R2000 (confirmed: the
   spec's per-entity section headers, e.g. "20.4.21 LINE (19)", list
   the same numeric code regardless of version column). See
   reverse/DWG_R1314_format_reference.md. */
#define DWG_R1314_TYPE_TEXT         0x01
#define DWG_R1314_TYPE_INSERT       0x07
#define DWG_R1314_TYPE_VERTEX2D     0x0A
#define DWG_R1314_TYPE_POLYLINE2D   0x0F
#define DWG_R1314_TYPE_ARC          0x11
#define DWG_R1314_TYPE_CIRCLE       0x12
#define DWG_R1314_TYPE_LINE         0x13
#define DWG_R1314_TYPE_POINT        0x1B
#define DWG_R1314_TYPE_SOLID        0x1F
#define DWG_R1314_TYPE_BLOCK_HEADER 0x31
#define DWG_R1314_TYPE_LAYER        0x33

#define DWG_R1314_MAX_LAYER_NAME 256
#define DWG_R1314_MAX_BLOCK_NAME 256

/* Safety bound on VERTEX chain walking -- same value and rationale as
   the R2000 reader's DWG_R2000_MAX_VERTICES, guarding a malformed/
   cyclic handle chain in a real file, never expected to trigger on a
   well-formed one. */
#define DWG_R1314_MAX_VERTICES 100000UL

/* Same idea, for walking a BLOCK_HEADER's own entity-definition chain
   (see decode_and_transform_block_entity_r1314 below) -- mirrors the
   R2000 reader's DWG_R2000_MAX_BLOCK_ENTITIES. */
#define DWG_R1314_MAX_BLOCK_ENTITIES 100000UL

/* Generous but real sanity bound on a decoded object's declared byte
   length -- a real entity in a real file is never anywhere near this
   large; used as a second line of defense (beyond the file-size bounds
   check) against a stale/garbage object-map entry that happens to land
   in-bounds but doesn't point at a real object. See dwg_r1314_reader.h
   for why this defensive layer exists (real R14 files can have a large
   fraction of orphaned map entries, confirmed against a real 4MB
   architectural sample). */
#define DWG_R1314_MAX_PLAUSIBLE_LENGTH 1000000UL

/* Same real hang, same fix, as dwg_r2000_entity_reader.c's own
   DWG_R2000_MAX_REACTORS -- see that constant's comment for the full
   story (a garbage Numreactors value from a stale object-map entry
   drove a `for` loop billions of iterations, no inherent bound). */
#define DWG_R1314_MAX_REACTORS 1000UL

typedef struct
{
    unsigned long obj_size_bits;
    unsigned long handle;
    unsigned short color;
    unsigned long entmode;
    unsigned long numreactors;
    unsigned long isbylayerlt;
    unsigned long nolinks;
} DWG_R1314_COMMON_ENTITY;

static void skip_eed_blocks(DWG_BITSTREAM *bs, unsigned short first_length)
{
    unsigned short length = first_length;

    while (length != 0U)
    {
        unsigned char code;
        unsigned long value;
        unsigned short i;

        dwg_bs_read_handle(bs, &code, &value);

        for (i = 0U; i < length; i++)
            (void)dwg_bs_read_bits(bs, 8UL);

        length = dwg_bs_read_bs(bs);
    }
}

/* Real bug found+fixed: `dwg_r2000_parse_object_map`'s own carving
   fallback (see dwg_r2000_reader.c, R2000_CARVE_ENTRY et al, added
   earlier this session) verifies/repairs a candidate location by
   peeking `RL obj_size THEN H handle` -- R2000's real field order.
   R13/R14's real order is DIFFERENT (handle comes right after Type,
   THEN EED, THEN preview, THEN obj_size -- see
   read_common_entity_data_r1314's own comment, confirmed against the
   ODA spec), so that shared repair silently mismatches every field
   read for THIS format, finding far fewer real objects than a
   correctly-ordered scan would (confirmed empirically: same
   technique, same file, ~9,385 candidates with R2000's field order
   vs a real object count near 57,937 the DXF confirms should exist).
   This redoes the SAME global-carving technique with R14's actual
   field order, as a second repair pass over the object map
   `dwg_r2000_parse_object_map` already returned. */
typedef struct
{
    unsigned long handle;
    unsigned long location;
} R1314_CARVE_ENTRY;

static int r1314_carve_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const R1314_CARVE_ENTRY *)a)->handle;
    unsigned long hb = ((const R1314_CARVE_ENTRY *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

/* Peeks just far enough to read a candidate object's own handle,
   using R14's real order (Type -> Handle), unlike R2000's (Type ->
   obj_size -> Handle). Doesn't need to skip EED/preview at all for
   this purpose -- the handle comes BEFORE either of those here. */
static long r1314_peek_handle(const unsigned char *data, unsigned long length, unsigned long loc,
                              unsigned long *out_handle)
{
    DWG_BITSTREAM bs;
    unsigned long len;
    unsigned short type;
    unsigned char code;
    unsigned long value;

    if (loc + 8UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    len = dwg_bs_read_ms(&bs);
    if (len < 4UL || len > length)
        return 0L;
    type = dwg_bs_read_bs(&bs);
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type < 5000U)))
        return 0L;
    dwg_bs_read_handle(&bs, &code, &value);
    if (code != 0U || value == 0UL || value > 5000000UL)
        return 0L;

    *out_handle = value;
    return 1L;
}

static R1314_CARVE_ENTRY *r1314_build_carve_index(const unsigned char *data, unsigned long length,
                                                   unsigned long *out_count)
{
    unsigned long pos, count = 0UL, capacity = 65536UL;
    R1314_CARVE_ENTRY *arr = (R1314_CARVE_ENTRY *)malloc(capacity * sizeof(R1314_CARVE_ENTRY));

    *out_count = 0UL;
    if (arr == NULL)
        return NULL;

    for (pos = 0UL; pos + 8UL < length; pos++)
    {
        unsigned long handle;
        if (!r1314_peek_handle(data, length, pos, &handle))
            continue;

        if (count >= capacity)
        {
            R1314_CARVE_ENTRY *bigger;
            capacity *= 2UL;
            bigger = (R1314_CARVE_ENTRY *)realloc(arr, capacity * sizeof(R1314_CARVE_ENTRY));
            if (bigger == NULL)
                break;
            arr = bigger;
        }
        arr[count].handle = handle;
        arr[count].location = pos;
        count++;
    }

    qsort(arr, (size_t)count, sizeof(R1314_CARVE_ENTRY), r1314_carve_cmp);
    *out_count = count;
    return arr;
}

static long r1314_carve_lookup(const R1314_CARVE_ENTRY *arr, unsigned long count,
                               unsigned long handle, unsigned long *out_loc)
{
    long lo = 0L, hi = (long)count - 1L;

    while (lo <= hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (arr[mid].handle == handle)
        {
            *out_loc = arr[mid].location;
            return 1L;
        }
        if (arr[mid].handle < handle)
            lo = mid + 1L;
        else
            hi = mid - 1L;
    }
    return 0L;
}

static void r1314_repair_objmap(const unsigned char *data, unsigned long length, DWG_R2000_OBJMAP *objmap)
{
    R1314_CARVE_ENTRY *carve_index;
    unsigned long carve_count, i;

    carve_index = r1314_build_carve_index(data, length, &carve_count);
    if (carve_index == NULL)
        return;

    for (i = 0UL; i < objmap->count; i++)
    {
        unsigned long existing_handle, carved_loc;

        if (r1314_peek_handle(data, length, objmap->entries[i].location, &existing_handle) &&
            existing_handle == objmap->entries[i].handle)
            continue; /* already correct */

        if (r1314_carve_lookup(carve_index, carve_count, objmap->entries[i].handle, &carved_loc))
            objmap->entries[i].location = carved_loc;
    }

    free(carve_index);
}

/*
 * Decodes the R13-R14 Common Entity Data fields from right after the
 * object's Type field through Invisibility, leaving bs positioned at
 * the start of the entity-specific fields. Field ORDER genuinely
 * differs from R2000 here (Obj size moves from right-after-Type to
 * right-before-Entmode, an extra Isbylayerlt bit exists, Ltype/
 * Plotstyle flags and Lineweight don't exist yet) -- see
 * reverse/DWG_R1314_format_reference.md for the full derivation,
 * validated byte-for-byte against the ODA spec's own real R14 worked
 * examples (ARC/CIRCLE/LINE/POINT) before this was trusted.
 * If a cached preview is present, its bytes are skipped (not parsed
 * as geometry) so the rest of Common Entity Data can still be read;
 * returns 0 only if the preview_size itself looks corrupt.
 */
static int read_common_entity_data_r1314(DWG_BITSTREAM *bs, DWG_R1314_COMMON_ENTITY *out)
{
    unsigned char handle_code;
    unsigned short eed_size;
    unsigned long preview_exists;
    double ltscale;
    unsigned short invisibility;

    dwg_bs_read_handle(bs, &handle_code, &out->handle);

    eed_size = dwg_bs_read_bs(bs);
    if (eed_size != 0U)
        skip_eed_blocks(bs, eed_size);

    /* Same real bug as R2000's own read_common_entity_data (see
       dwg_r2000_entity_reader.c) -- this bit is `preview_exists`, a
       genuine per-entity cached preview, not "entity unreadable".
       R13/R14 falls in the SAME "VERSIONS(R_13b1, R_2007)" spec range
       as R2000, so `preview_size` is a plain raw 32-bit RL here too
       (confirmed against LibreDWG's real common_entity_data.spec --
       only R_2010b+ uses the compressed BLL form). Skip the preview
       bytes and keep reading instead of discarding the whole entity. */
    preview_exists = dwg_bs_read_bit(bs);
    if (preview_exists != 0UL)
    {
        unsigned long preview_size = dwg_bs_read_rl(bs);
        if (preview_size > 0x7FFFFFFFUL)
            return 0;
        dwg_bs_seek_bit(bs, dwg_bs_tell_bit(bs) + preview_size * 8UL);
    }

    out->obj_size_bits = dwg_bs_read_rl(bs);

    out->entmode = dwg_bs_read_bb(bs);
    out->numreactors = dwg_bs_read_bl(bs);
    if (out->numreactors > DWG_R1314_MAX_REACTORS)
        out->numreactors = DWG_R1314_MAX_REACTORS; /* clamp, don't trust raw garbage -- see the constant's comment */
    out->isbylayerlt = dwg_bs_read_bit(bs);
    out->nolinks = dwg_bs_read_bit(bs);

    out->color = dwg_bs_read_bs(bs); /* R15 and earlier: plain ACI index BS */
    ltscale = dwg_bs_read_bd(bs);
    invisibility = dwg_bs_read_bs(bs);
    (void)ltscale; (void)invisibility;

    return 1;
}

/*
 * Finds this entity's LAYER handle. Order genuinely differs from
 * R2000's read_entity_layer_handle: R13-R14's Common Entity Handle Data
 * puts LAYER right after xdicobjhandle, BEFORE the prev/next entity
 * links (R2000 puts LAYER AFTER them) -- see the spec's section 20.4.2,
 * "R13-R14 Only" vs "R13-R2000 Only" ordering.
 */
static int read_entity_layer_handle_r1314(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                          const DWG_R1314_COMMON_ENTITY *common,
                                          unsigned long *layer_handle)
{
    unsigned long i;
    unsigned char code;
    unsigned long value;

    dwg_bs_seek_bit(bs, ms_end_bit + common->obj_size_bits);

    if (common->entmode == 0UL)
        dwg_bs_read_handle(bs, &code, &value); /* owner ref handle, not used yet */

    for (i = 0UL; i < common->numreactors; i++)
        dwg_bs_read_handle(bs, &code, &value); /* reactor handles, not used yet */

    dwg_bs_read_handle(bs, &code, &value); /* xdicobjhandle, not used yet */

    dwg_bs_read_handle(bs, &code, layer_handle); /* LAYER: always present, R13-R14 order */

    return 1;
}

/*
 * Decodes just enough of a table-record object to get its name -- R13-
 * R14 counterpart of the R2000 reader's decode_table_record_name.
 * Common non-entity object format's Obj-size field also moves position
 * for R13-R14 (after Handle+EED, right before Numreactors) -- confirmed
 * against the spec's section 20.1 "R13-R14" column.
 */
/* out_color: same idea and same real motivation as the R2000 reader's
   own decode_table_record_name -- see that function's comment. R13-R14
   layout differs after the name: four separate B flags (Frozen/On/
   Frz-in-new/Locked) instead of R2000+'s one combined "Values BS",
   everything else (64-flag/xrefindex+1/Xdep before, Color right after)
   is the same shape -- see spec p.166-167, section 20.4.54. */
static int decode_table_record_name_r1314(const unsigned char *data, unsigned long length,
                                          unsigned long loc, unsigned short expected_type,
                                          char *name_buf, unsigned long buf_size,
                                          unsigned short *out_color)
{
    DWG_BITSTREAM bs;
    unsigned short obj_type;
    unsigned short eed_size;
    unsigned char handle_code;
    unsigned long handle_value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != expected_type)
        return 0;

    dwg_bs_read_handle(&bs, &handle_code, &handle_value);

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks(&bs, eed_size);

    (void)dwg_bs_read_rl(&bs); /* obj_size: R13-R14 position, not needed here (sequential read) */
    (void)dwg_bs_read_bl(&bs); /* numreactors */

    (void)dwg_bs_read_t(&bs, name_buf, (unsigned short)buf_size);

    if (out_color != NULL && expected_type == DWG_R1314_TYPE_LAYER)
    {
        (void)dwg_bs_read_bit(&bs);  /* 64-flag */
        (void)dwg_bs_read_bs(&bs);   /* xrefindex+1 */
        (void)dwg_bs_read_bit(&bs);  /* Xdep */
        (void)dwg_bs_read_bit(&bs);  /* Frozen (R13-R14 only) */
        (void)dwg_bs_read_bit(&bs);  /* On (R13-R14 only) */
        (void)dwg_bs_read_bit(&bs);  /* Frz-in-new (R13-R14 only) */
        (void)dwg_bs_read_bit(&bs);  /* Locked (R13-R14 only) */
        *out_color = dwg_bs_read_bs(&bs); /* Color: R15 and earlier is a plain BS index */
    }

    return 1;
}

static void apply_color(HENTITY e, unsigned short color)
{
    if (color != 0U && color != 256U)
        dwg_entity_put_color(e, color);
}

/* Coordinate sanity check -- see dwg_r2000_entity_reader.c's identical
   is_plausible_coord for the full rationale (real, visible bug: a
   stale object-map entry's garbage bytes decoded a LINE with
   coordinates like 1e101/1e87, which dominated zoom-to-fit and scaled
   the entire real drawing down to an invisible point). Same 10M
   threshold, same reasoning, applied here too since R13/R14 files hit
   the identical object-map-noise phenomenon. */
#define DWG_R1314_MAX_PLAUSIBLE_COORD 1.0e7

static int is_plausible_coord(double v)
{
    return v > -DWG_R1314_MAX_PLAUSIBLE_COORD && v < DWG_R1314_MAX_PLAUSIBLE_COORD;
}

/* LINE (0x13): R13-R14 field layout is plain Start/End 3BD points, not
   R2000's Z's-are-zero-bit + DD-default scheme. */
static HENTITY decode_line_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D start, end, extrusion;

    dwg_bs_read_3bd(bs, &start);
    dwg_bs_read_3bd(bs, &end);
    (void)dwg_bs_read_bd(bs); /* thickness: plain BD for R13-R14, not modeled yet */
    dwg_bs_read_3bd(bs, &extrusion); /* plain 3BD for R13-R14, not modeled yet */

    if (!is_plausible_coord(start.x) || !is_plausible_coord(start.y) || !is_plausible_coord(start.z) ||
        !is_plausible_coord(end.x) || !is_plausible_coord(end.y) || !is_plausible_coord(end.z))
        return NULL;

    return dwg_add_line(hDwg, start.x, start.y, start.z, end.x, end.y, end.z);
}

static HENTITY decode_circle_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D center, extrusion;
    double radius;

    dwg_bs_read_3bd(bs, &center);
    radius = dwg_bs_read_bd(bs);
    (void)dwg_bs_read_bd(bs); /* thickness */
    dwg_bs_read_3bd(bs, &extrusion);

    if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
        !is_plausible_coord(radius))
        return NULL;

    return dwg_add_circle(hDwg, center.x, center.y, center.z, radius);
}

static HENTITY decode_arc_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D center, extrusion;
    double radius, start_angle_rad, end_angle_rad;

    dwg_bs_read_3bd(bs, &center);
    radius = dwg_bs_read_bd(bs);
    (void)dwg_bs_read_bd(bs); /* thickness */
    dwg_bs_read_3bd(bs, &extrusion);
    start_angle_rad = dwg_bs_read_bd(bs);
    end_angle_rad = dwg_bs_read_bd(bs);

    if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
        !is_plausible_coord(radius))
        return NULL;

    return dwg_add_arc(hDwg, center.x, center.y, center.z, radius,
                       start_angle_rad * 180.0 / M_PI, end_angle_rad * 180.0 / M_PI);
}

static HENTITY decode_point_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D p, extrusion;

    dwg_bs_read_3bd(bs, &p);
    (void)dwg_bs_read_bd(bs); /* thickness */
    dwg_bs_read_3bd(bs, &extrusion);
    (void)dwg_bs_read_bd(bs); /* X-axis angle: no field to store it in, same as R2000 reader */

    if (!is_plausible_coord(p.x) || !is_plausible_coord(p.y) || !is_plausible_coord(p.z))
        return NULL;

    return dwg_add_point(hDwg, p.x, p.y, p.z);
}

/* SOLID (0x1F): same field shape as R2000 (no version split in the
   spec's own table) -- only Thickness/Extrusion's encoding differs
   (generic BT/BE rule, see the header comment above decode_line_r1314). */
static HENTITY decode_solid_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    double elevation;
    double x1, y1, x2, y2, x3, y3, x4, y4;
    DWG_POINT3D extrusion;
    HENTITY e;

    (void)dwg_bs_read_bd(bs); /* thickness */
    elevation = dwg_bs_read_bd(bs);
    x1 = dwg_bs_read_rd(bs); y1 = dwg_bs_read_rd(bs);
    x2 = dwg_bs_read_rd(bs); y2 = dwg_bs_read_rd(bs);
    x3 = dwg_bs_read_rd(bs); y3 = dwg_bs_read_rd(bs);
    x4 = dwg_bs_read_rd(bs); y4 = dwg_bs_read_rd(bs);
    dwg_bs_read_3bd(bs, &extrusion);

    if (!is_plausible_coord(x1) || !is_plausible_coord(y1) || !is_plausible_coord(x2) || !is_plausible_coord(y2) ||
        !is_plausible_coord(x3) || !is_plausible_coord(y3) || !is_plausible_coord(x4) || !is_plausible_coord(y4) ||
        !is_plausible_coord(elevation))
        return NULL;

    e = dwg_add_solid(hDwg, x1, y1, elevation, x2, y2, elevation,
                      x3, y3, elevation, x4, y4, elevation);

    return e;
}

/* TEXT (0x01): R13-R14 field layout is a plain, unconditional sequence
   (no DataFlags gating at all, unlike R2000's byte-saving scheme) --
   see the spec's section 20.4.3 "R13-14 Only" column. STYLE handle
   resolution deliberately deferred (cosmetic only, not needed to see
   geometry render) -- a documented scope gap, not an oversight. */
static HENTITY decode_text_r1314(HDWG hDwg, DWG_BITSTREAM *bs)
{
    double elevation, oblique_rad, rotation_rad, height, width_factor;
    DWG_POINT3D ins, align, extrusion;
    unsigned short horiz;
    char text_buf[DWG_TEXT_MAX];
    HENTITY e;

    elevation = dwg_bs_read_bd(bs);
    ins.x = dwg_bs_read_rd(bs);
    ins.y = dwg_bs_read_rd(bs);
    ins.z = elevation;
    align.x = dwg_bs_read_rd(bs);
    align.y = dwg_bs_read_rd(bs);
    align.z = elevation;
    dwg_bs_read_3bd(bs, &extrusion);
    (void)dwg_bs_read_bd(bs); /* thickness */
    oblique_rad = dwg_bs_read_bd(bs);
    rotation_rad = dwg_bs_read_bd(bs);
    height = dwg_bs_read_bd(bs);
    width_factor = dwg_bs_read_bd(bs);

    (void)dwg_bs_read_t(bs, text_buf, (unsigned short)sizeof(text_buf));

    (void)dwg_bs_read_bs(bs); /* generation: not modeled */
    horiz = dwg_bs_read_bs(bs);
    (void)dwg_bs_read_bs(bs); /* vertical align: not modeled */

    if (!is_plausible_coord(ins.x) || !is_plausible_coord(ins.y) || !is_plausible_coord(elevation) ||
        !is_plausible_coord(height))
        return NULL;

    e = dwg_add_text(hDwg, ins.x, ins.y, elevation, height, rotation_rad * 180.0 / M_PI, text_buf);
    if (e != NULL)
    {
        dwg_text_set_point0(e, align.x, align.y, elevation);
        dwg_text_set_width_factor(e, width_factor);
        dwg_text_set_oblique(e, oblique_rad * 180.0 / M_PI);
        dwg_text_set_align(e, horiz);
    }

    return e;
}

/* R13-R14 counterpart of the R2000 reader's decode_block_header --
   same field shape (spec p.163-165, section 20.4.52) except: Obj size
   moves to the R13-R14 position (after Numreactors, generic rule
   already established), and the "Insert Count RC*" sequence (a run of
   non-zero bytes terminated by 0, used to size the trailing Insert
   Handles array) is R2000+ ONLY -- absent entirely for R13-R14, so
   nothing to skip there (goes straight from Xref pname to Block
   Description instead). "Loaded Bit" is likewise R2000+ only. Returns
   0 if this isn't really a BLOCK_HEADER, or it's an xref/overlaid-xref
   block (the first/last-entity handle pair genuinely isn't stored in
   that case). */
static int decode_block_header_r1314(const unsigned char *data, unsigned long length,
                                     unsigned long loc, DWG_POINT3D *base,
                                     unsigned long *first_entity_handle,
                                     unsigned long *last_entity_handle)
{
    DWG_BITSTREAM bs;
    unsigned short obj_type;
    unsigned short eed_size;
    unsigned char handle_code;
    unsigned long handle_value;
    unsigned long numreactors, i;
    unsigned long is_xref, is_xref_overlaid;
    unsigned long own_handle;
    char name_buf[DWG_R1314_MAX_BLOCK_NAME];
    char xref_pname[DWG_R1314_MAX_BLOCK_NAME];
    char block_desc[DWG_R1314_MAX_BLOCK_NAME];
    unsigned long preview_size, j;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R1314_TYPE_BLOCK_HEADER)
        return 0;

    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* code 0: this object's own absolute handle */
    own_handle = handle_value;

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks(&bs, eed_size);

    (void)dwg_bs_read_rl(&bs); /* obj_size: R13-R14 position */
    numreactors = dwg_bs_read_bl(&bs);
    if (numreactors > DWG_R1314_MAX_REACTORS)
        numreactors = DWG_R1314_MAX_REACTORS;

    (void)dwg_bs_read_t(&bs, name_buf, (unsigned short)sizeof(name_buf));

    (void)dwg_bs_read_bit(&bs); /* 64-flag */
    (void)dwg_bs_read_bs(&bs);  /* xrefindex+1 */
    (void)dwg_bs_read_bit(&bs); /* Xdep */
    (void)dwg_bs_read_bit(&bs); /* Anonymous */
    (void)dwg_bs_read_bit(&bs); /* Hasatts */
    is_xref = dwg_bs_read_bit(&bs);
    is_xref_overlaid = dwg_bs_read_bit(&bs);
    /* no "Loaded Bit" here -- R2000+ only */

    dwg_bs_read_3bd(&bs, base);
    (void)dwg_bs_read_t(&bs, xref_pname, (unsigned short)sizeof(xref_pname));
    /* no "Insert Count" run here -- R2000+ only */
    (void)dwg_bs_read_t(&bs, block_desc, (unsigned short)sizeof(block_desc));

    preview_size = dwg_bs_read_bl(&bs);
    if (preview_size > (unsigned long)length)
        return 0;
    for (j = 0UL; j < preview_size; j++)
        (void)dwg_bs_read_rc(&bs);

    (void)dwg_bs_read_rc(&bs); /* Block scaling */

    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* Block control handle */
    for (i = 0UL; i < numreactors; i++)
        dwg_bs_read_handle(&bs, &handle_code, &handle_value);
    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* xdicobjhandle */
    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* NULL */
    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* BLOCK entity */

    if (is_xref != 0UL || is_xref_overlaid != 0UL)
        return 0;

    dwg_bs_read_handle(&bs, &handle_code, &handle_value);
    *first_entity_handle = dwg_bs_resolve_handle(handle_code, handle_value, own_handle);
    dwg_bs_read_handle(&bs, &handle_code, &handle_value);
    *last_entity_handle = dwg_bs_resolve_handle(handle_code, handle_value, own_handle);

    return 1;
}

/* Finds hEntity's own "next" handle in its Common Entity Handle Data --
   R13-R14 order this time (owner/reactors/xdic/LAYER/[LTYPE]/[prev,
   next] -- LAYER comes BEFORE prev/next here, opposite of R2000, same
   reordering already established for read_entity_layer_handle_r1314).
   Nolinks==1 means the implicit default (own handle + 1) applies. */
static unsigned long find_entity_next_handle_r1314(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                                    const DWG_R1314_COMMON_ENTITY *common,
                                                    unsigned long cur_handle)
{
    unsigned long i;
    unsigned char code;
    unsigned long value;

    if (common->nolinks != 0UL)
        return cur_handle + 1UL;

    dwg_bs_seek_bit(bs, ms_end_bit + common->obj_size_bits);

    if (common->entmode == 0UL)
        dwg_bs_read_handle(bs, &code, &value);
    for (i = 0UL; i < common->numreactors; i++)
        dwg_bs_read_handle(bs, &code, &value);
    dwg_bs_read_handle(bs, &code, &value); /* xdicobjhandle */
    dwg_bs_read_handle(bs, &code, &value); /* LAYER: R13-R14 order, before prev/next */
    if (common->isbylayerlt == 0UL)
        dwg_bs_read_handle(bs, &code, &value); /* LTYPE */
    dwg_bs_read_handle(bs, &code, &value); /* previous entity */
    dwg_bs_read_handle(bs, &code, &value); /* next entity -- the one we want */

    return dwg_bs_resolve_handle(code, value, cur_handle);
}

/* R13-R14 counterpart of the R2000 reader's
   decode_and_transform_block_entity -- same idea (decode one entity
   from inside a BLOCK's own definition, transform it by the INSERT's
   scale/rotate/move, same order already confirmed via dwg_transform.h's
   own dwg_entity_explode documentation), same type coverage (LINE/
   CIRCLE/ARC/POINT/SOLID only). */
static int decode_and_transform_block_entity_r1314(HDWG hDwg, const unsigned char *data, unsigned long length,
                                                    unsigned long loc, unsigned long cur_handle,
                                                    const DWG_POINT3D *block_base,
                                                    double ins_x, double ins_y, double ins_z,
                                                    double scale_x, double scale_y, double scale_z,
                                                    double rotation_deg,
                                                    unsigned long *next_handle)
{
    DWG_BITSTREAM bs;
    unsigned long ms_end_bit;
    unsigned short obj_type;
    DWG_R1314_COMMON_ENTITY common;
    HENTITY e = NULL;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    ms_end_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);

    if (obj_type != DWG_R1314_TYPE_LINE && obj_type != DWG_R1314_TYPE_CIRCLE &&
        obj_type != DWG_R1314_TYPE_ARC && obj_type != DWG_R1314_TYPE_POINT &&
        obj_type != DWG_R1314_TYPE_SOLID)
        return 0;

    if (!read_common_entity_data_r1314(&bs, &common))
        return 0;

    switch (obj_type)
    {
    case DWG_R1314_TYPE_LINE:   e = decode_line_r1314(hDwg, &bs);   break;
    case DWG_R1314_TYPE_CIRCLE: e = decode_circle_r1314(hDwg, &bs); break;
    case DWG_R1314_TYPE_ARC:    e = decode_arc_r1314(hDwg, &bs);    break;
    case DWG_R1314_TYPE_POINT:  e = decode_point_r1314(hDwg, &bs);  break;
    case DWG_R1314_TYPE_SOLID:  e = decode_solid_r1314(hDwg, &bs);  break;
    default: break;
    }

    *next_handle = find_entity_next_handle_r1314(&bs, ms_end_bit, &common, cur_handle);

    if (e == NULL)
        return 1;

    apply_color(e, common.color);

    dwg_entity_move(e, -block_base->x, -block_base->y, -block_base->z);
    dwg_entity_scale_xyz(e, 0.0, 0.0, 0.0, scale_x, scale_y, scale_z);
    dwg_entity_rotate(e, 0.0, 0.0, 0.0, rotation_deg);
    dwg_entity_move(e, ins_x, ins_y, ins_z);

    return 1;
}

static unsigned char *read_whole_file(const char *path, unsigned long *out_length)
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

/* Binary search, not linear -- see the identical comment in
   dwg_r2000_entity_reader.c's own objmap_find, same root cause and
   fix, the object-map's ascending-by-handle invariant applies equally
   here (it's a property of the shared object-map format, not of
   either reader). This was the R13/R14 reader's own version of the
   same real hang, hit on Arturo's real files while testing this
   subsystem. */
static int objmap_find(const DWG_R2000_OBJMAP *objmap, unsigned long handle, unsigned long *location)
{
    unsigned long lo = 0UL, hi = objmap->count;

    while (lo < hi)
    {
        unsigned long mid = lo + (hi - lo) / 2UL;

        if (objmap->entries[mid].handle == handle)
        {
            *location = objmap->entries[mid].location;
            return 1;
        }
        else if (objmap->entries[mid].handle < handle)
            lo = mid + 1UL;
        else
            hi = mid;
    }

    return 0;
}

/* Skips owner/reactors/xdicobjhandle/LAYER/[LTYPE]/[prev,next] --
   everything Common Entity Handle Data has for R13-R14 (see the header
   comment on read_entity_layer_handle_r1314 for why LAYER comes before
   prev/next here, unlike R2000) -- leaving bs positioned right where
   an entity-specific extra handle (2D POLYLINE's first/last VERTEX)
   would begin. */
static void skip_to_after_common_handles_r1314(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                               const DWG_R1314_COMMON_ENTITY *common)
{
    unsigned long i;
    unsigned char code;
    unsigned long value;

    dwg_bs_seek_bit(bs, ms_end_bit + common->obj_size_bits);

    if (common->entmode == 0UL)
        dwg_bs_read_handle(bs, &code, &value);

    for (i = 0UL; i < common->numreactors; i++)
        dwg_bs_read_handle(bs, &code, &value);

    dwg_bs_read_handle(bs, &code, &value); /* xdicobjhandle */
    dwg_bs_read_handle(bs, &code, &value); /* LAYER: R13-R14 order, before prev/next */

    if (common->isbylayerlt == 0UL)
        dwg_bs_read_handle(bs, &code, &value); /* LTYPE */

    if (common->nolinks == 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* previous entity */
        dwg_bs_read_handle(bs, &code, &value); /* next entity */
    }
}

/*
 * INSERT (0x07), R13-R14 column (spec p.115-116, section 20.4.9):
 * X/Y/Z Scale are three plain BD fields, not R2000+'s DataFlags-gated
 * scheme -- same "simpler, unconditional" pattern already seen for
 * this version's LINE/TEXT. BLOCK HEADER handle sits immediately after
 * the ENTIRE Common Entity Handle Data block (which for R13-R14
 * already includes LAYER/[LTYPE]/[prev,next] -- see
 * skip_to_after_common_handles_r1314), unlike R2000 where it comes
 * right after xdicobjhandle. Real "accesorios" (door/window/fixture
 * blocks): explodes the referenced BLOCK_HEADER's own entity chain
 * into the main document at read time, same design and same transform
 * order as the R2000 reader's own decode_insert.
 */
static HENTITY decode_insert_r1314(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                   const DWG_R1314_COMMON_ENTITY *common,
                                   const unsigned char *data, unsigned long length,
                                   const DWG_R2000_OBJMAP *objmap)
{
    DWG_POINT3D ins, extrusion, block_base_pt;
    double sx, sy, sz, rotation_rad;
    unsigned long has_attribs;
    unsigned char code;
    unsigned long block_header_handle, block_header_loc;
    unsigned long first_block_entity, last_block_entity;
    char block_name[DWG_R1314_MAX_BLOCK_NAME];
    HENTITY e;

    dwg_bs_read_3bd(bs, &ins);
    sx = dwg_bs_read_bd(bs);
    sy = dwg_bs_read_bd(bs);
    sz = dwg_bs_read_bd(bs);
    rotation_rad = dwg_bs_read_bd(bs);
    dwg_bs_read_3bd(bs, &extrusion);
    has_attribs = dwg_bs_read_bit(bs);
    (void)has_attribs; /* only affects trailing ATTRIB handles, not read here */

    if (!is_plausible_coord(ins.x) || !is_plausible_coord(ins.y) || !is_plausible_coord(ins.z) ||
        !is_plausible_coord(sx) || !is_plausible_coord(sy) || !is_plausible_coord(sz))
        return NULL;

    block_name[0] = '\0';
    block_header_loc = 0UL;
    skip_to_after_common_handles_r1314(bs, ms_end_bit, common);
    dwg_bs_read_handle(bs, &code, &block_header_handle);

    if (objmap_find(objmap, block_header_handle, &block_header_loc) &&
        block_header_loc + 6UL < length)
    {
        decode_table_record_name_r1314(data, length, block_header_loc, DWG_R1314_TYPE_BLOCK_HEADER,
                                       block_name, sizeof(block_name), NULL);
    }
    else
    {
        block_header_loc = length; /* sentinel: unresolved, decode_block_header_r1314's own bounds/type checks will cleanly reject this */
    }

    /* file stores angle in radians; this engine's API uses degrees. */
    e = dwg_add_insert(hDwg, block_name, ins.x, ins.y, ins.z, rotation_rad * 180.0 / M_PI);
    if (e != NULL)
        dwg_insert_set_scale(e, sx, sy, sz);

    if (block_header_loc < length &&
        decode_block_header_r1314(data, length, block_header_loc, &block_base_pt, &first_block_entity, &last_block_entity))
    {
        unsigned long cur = first_block_entity;
        unsigned long count;

        for (count = 0UL; count < DWG_R1314_MAX_BLOCK_ENTITIES; count++)
        {
            unsigned long eloc, next;
            int reached_last = (cur == last_block_entity);

            if (!objmap_find(objmap, cur, &eloc))
                break;
            if (eloc + 6UL >= length)
                break;

            if (!decode_and_transform_block_entity_r1314(hDwg, data, length, eloc, cur, &block_base_pt,
                                                         ins.x, ins.y, ins.z, sx, sy, sz,
                                                         rotation_rad * 180.0 / M_PI, &next))
                break;

            if (reached_last)
                break;

            cur = next;
        }
    }

    return e;
}

/* R13-R14 counterpart of the R2000 reader's decode_vertex2d_and_advance
   -- same overall shape (decode the vertex, then find its "next" link
   to continue the chain), but the handle order to reach "next" differs:
   R13-R14 has LAYER/[LTYPE] BEFORE prev/next (see
   skip_to_after_common_handles_r1314), so this reads through those
   first instead of stopping right after xdicobjhandle like the R2000
   version does. */
static int decode_vertex2d_and_advance_r1314(const unsigned char *data, unsigned long length,
                                              unsigned long loc, unsigned long cur_handle,
                                              HPOLYLINE pl, double elevation,
                                              unsigned long *next_handle)
{
    DWG_BITSTREAM bs;
    unsigned long ms_end_bit;
    unsigned short obj_type;
    DWG_R1314_COMMON_ENTITY common;
    double x, y, start_width, end_width, bulge, raw_start_width;
    unsigned char code;
    unsigned long value, i;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    ms_end_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R1314_TYPE_VERTEX2D)
        return 0;

    if (!read_common_entity_data_r1314(&bs, &common))
        return 0;

    (void)dwg_bs_read_rc(&bs); /* per-vertex flags ("EC" in the spec -- same RC read already confirmed correct for R2000, reused here) */
    {
        DWG_POINT3D p;
        dwg_bs_read_3bd(&bs, &p);
        x = p.x;
        y = p.y;
    }

    raw_start_width = dwg_bs_read_bd(&bs);
    if (raw_start_width < 0.0)
    {
        end_width = -raw_start_width;
        start_width = end_width;
    }
    else
    {
        start_width = raw_start_width;
        end_width = dwg_bs_read_bd(&bs);
    }
    bulge = dwg_bs_read_bd(&bs);
    (void)dwg_bs_read_bd(&bs); /* tangent direction: not modeled yet */

    if (!is_plausible_coord(x) || !is_plausible_coord(y) || !is_plausible_coord(elevation))
        return 0; /* garbage vertex -- stop walking this chain rather than adding it */

    dwg_polyline_add_vertex2(pl, x, y, elevation, bulge, start_width, end_width);

    if (common.nolinks != 0UL)
    {
        *next_handle = cur_handle + 1UL;
        return 1;
    }

    dwg_bs_seek_bit(&bs, ms_end_bit + common.obj_size_bits);

    if (common.entmode == 0UL)
        dwg_bs_read_handle(&bs, &code, &value);

    for (i = 0UL; i < common.numreactors; i++)
        dwg_bs_read_handle(&bs, &code, &value);

    dwg_bs_read_handle(&bs, &code, &value); /* xdicobjhandle */
    dwg_bs_read_handle(&bs, &code, &value); /* LAYER: R13-R14 order */
    if (common.isbylayerlt == 0UL)
        dwg_bs_read_handle(&bs, &code, &value); /* LTYPE */
    dwg_bs_read_handle(&bs, &code, &value); /* previous entity */
    dwg_bs_read_handle(&bs, &code, &value); /* next entity -- the one we want */

    *next_handle = dwg_bs_resolve_handle(code, value, cur_handle);

    return 1;
}

/* 2D POLYLINE (type 0x0F): field shape is version-UNIFORM per the spec
   (no "R13-R14 Only" split for the fields themselves, only R2004+ adds
   an extra field this engine doesn't need) -- the only real difference
   from R2000 is Thickness/Extrusion being plain BD/3BD (see the
   generic BT/BE rule) and the handle order to reach first/last VERTEX
   (LAYER/[LTYPE] come before them here too, same reasoning as above).
   bs is already positioned at the entity-specific fields when called,
   same convention as decode_line_r1314 etc. */
static HENTITY decode_polyline2d_r1314(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                       const DWG_R1314_COMMON_ENTITY *common,
                                       const unsigned char *data, unsigned long length,
                                       const DWG_R2000_OBJMAP *objmap)
{
    unsigned short flags;
    double elevation;
    DWG_POINT3D extrusion;
    HENTITY e;
    HPOLYLINE pl;

    flags = dwg_bs_read_bs(bs);
    (void)dwg_bs_read_bs(bs); /* curve type: not modeled yet */
    (void)dwg_bs_read_bd(bs); /* default start width: not modeled (per-vertex widths are) */
    (void)dwg_bs_read_bd(bs); /* default end width */
    (void)dwg_bs_read_bd(bs); /* thickness: plain BD for R13-R14, not modeled yet */
    elevation = dwg_bs_read_bd(bs);
    dwg_bs_read_3bd(bs, &extrusion); /* plain 3BD for R13-R14, not modeled yet */

    e = dwg_add_polyline(hDwg);
    if (e == NULL)
        return NULL;

    pl = dwg_polyline_from_entity(e);
    if (pl == NULL)
        return e;

    dwg_polyline_set_closed(pl, (flags & DWG_POLYLINE_CLOSED) ? 1L : 0L);
    dwg_polyline_set_elevation(pl, elevation);

    {
        unsigned long first_vertex_handle, last_vertex_handle, cur_handle;
        unsigned char code;
        unsigned long value;
        unsigned long count;

        skip_to_after_common_handles_r1314(bs, ms_end_bit, common);

        dwg_bs_read_handle(bs, &code, &value);
        first_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);
        dwg_bs_read_handle(bs, &code, &value);
        last_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);

        /* Real handle numbers are never 0 -- a stale/orphaned object-map
           entry (see dwg_r1314_reader.h) can still decode a plausible
           MS length + BS type==0x0F by pure coincidence without being a
           real POLYLINE2D at all; first/last vertex handle landing on 0
           is a clear, checkable sign of exactly that (unlike e.g. a
           bogus LINE's coordinates, which look "valid" no matter what
           the bytes are). Treat it the same as any other failed
           sanity check: this isn't a real object, discard it rather
           than leaving a hollow 0-vertex polyline in the document. */
        if (first_vertex_handle == 0UL || last_vertex_handle == 0UL ||
            !objmap_find(objmap, first_vertex_handle, &value))
        {
            dwg_document_remove_entity(hDwg, e);
            return NULL;
        }

        cur_handle = first_vertex_handle;
        for (count = 0UL; count < DWG_R1314_MAX_VERTICES; count++)
        {
            unsigned long vloc, next_handle;
            int reached_last = (cur_handle == last_vertex_handle);

            if (!objmap_find(objmap, cur_handle, &vloc))
                break;
            if (vloc + 6UL >= length)
                break;

            if (!decode_vertex2d_and_advance_r1314(data, length, vloc, cur_handle, pl, elevation, &next_handle))
                break;

            if (reached_last)
                break;

            cur_handle = next_handle;
        }

        if (dwg_polyline_vertex_count(pl) == 0UL)
        {
            dwg_document_remove_entity(hDwg, e);
            return NULL;
        }
    }

    return e;
}

HDWG dwg_read_dwg_r1314(const char *path, DWG_IO_RESULT *result)
{
    unsigned char *data;
    unsigned long length;
    DWG_R2000_HEADER header;
    const DWG_R2000_SECTION_RECORD *objmap_rec;
    DWG_R2000_OBJMAP objmap;
    HDWG hDwg;
    unsigned long i;

    if (result != NULL)
        *result = DWG_IO_OK;

    if (path == NULL)
    {
        if (result != NULL) *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    data = read_whole_file(path, &length);
    if (data == NULL)
    {
        if (result != NULL) *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    if (!dwg_r2000_parse_header(data, length, &header) ||
        (memcmp(header.version, "AC1012", 6) != 0 && memcmp(header.version, "AC1014", 6) != 0))
    {
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    objmap_rec = dwg_r2000_find_record(&header, 2U);
    if (objmap_rec == NULL)
    {
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    if (!dwg_r2000_parse_object_map(data, length, objmap_rec->seeker, objmap_rec->size, &objmap))
    {
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }
    r1314_repair_objmap(data, length, &objmap);

#ifdef DBGPROXYFIND
    {
        unsigned long pos, found = 0UL;
        for (pos = 0UL; pos + 16UL < length && found < 5UL; pos++)
        {
            DWG_BITSTREAM pbs;
            unsigned long plen;
            unsigned short ptype;
            unsigned char pcode;
            unsigned long pvalue;

            dwg_bs_init(&pbs, data, length);
            dwg_bs_seek_bit(&pbs, pos * 8UL);
            plen = dwg_bs_read_ms(&pbs);
            if (plen < 4UL || plen > length) continue;
            ptype = dwg_bs_read_bs(&pbs);
            if (ptype != 0x1F2U) continue;
            dwg_bs_read_handle(&pbs, &pcode, &pvalue);
            if (pcode != 0U || pvalue == 0UL || pvalue > 5000000UL) continue;

            printf("DBGPROXYFIND #%lu pos=%lu handle=%lu\n", found, pos, pvalue);
            found++;
        }
        printf("DBGPROXYFIND total_found=%lu\n", found);
    }
#endif

    hDwg = dwg_document_create();
    if (hDwg == NULL)
    {
        dwg_r2000_objmap_free(&objmap);
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    for (i = 0UL; i < objmap.count; i++)
    {
        DWG_BITSTREAM bs;
        unsigned short obj_type;
        unsigned long declared_length;
        DWG_R1314_COMMON_ENTITY common;
        unsigned long ms_end_bit;
        HENTITY e = NULL;

        /* Defensive bounds check: real, heavily-edited R13/R14 files can
           have stale object-map entries whose location, despite passing
           the map's own section-level CRC, doesn't point at a real
           object (confirmed against a real 4MB sample -- see
           dwg_r1314_reader.h). 6 bytes of margin covers the MS length
           (up to 4 bytes) + BS type (up to 2 bytes) header read below. */
        if (objmap.entries[i].location + 6UL >= length)
            continue;

        dwg_bs_init(&bs, data, length);
        dwg_bs_seek_bit(&bs, objmap.entries[i].location * 8UL);

        declared_length = dwg_bs_read_ms(&bs);
        if (declared_length > DWG_R1314_MAX_PLAUSIBLE_LENGTH ||
            objmap.entries[i].location + declared_length > length)
            continue; /* second line of defense: implausible or out-of-file declared size */

        ms_end_bit = dwg_bs_tell_bit(&bs);
        obj_type = dwg_bs_read_bs(&bs);

        if (obj_type != DWG_R1314_TYPE_LINE && obj_type != DWG_R1314_TYPE_CIRCLE &&
            obj_type != DWG_R1314_TYPE_ARC && obj_type != DWG_R1314_TYPE_POINT &&
            obj_type != DWG_R1314_TYPE_TEXT && obj_type != DWG_R1314_TYPE_POLYLINE2D &&
            obj_type != DWG_R1314_TYPE_SOLID && obj_type != DWG_R1314_TYPE_INSERT)
            continue; /* not modeled (or, for a stale map entry, not a real object at all) */

        if (!read_common_entity_data_r1314(&bs, &common))
            continue;

        switch (obj_type)
        {
        case DWG_R1314_TYPE_LINE:   e = decode_line_r1314(hDwg, &bs);   break;
        case DWG_R1314_TYPE_CIRCLE: e = decode_circle_r1314(hDwg, &bs); break;
        case DWG_R1314_TYPE_ARC:    e = decode_arc_r1314(hDwg, &bs);    break;
        case DWG_R1314_TYPE_POINT:  e = decode_point_r1314(hDwg, &bs);  break;
        case DWG_R1314_TYPE_SOLID:  e = decode_solid_r1314(hDwg, &bs);  break;
        case DWG_R1314_TYPE_TEXT:   e = decode_text_r1314(hDwg, &bs);   break;
        case DWG_R1314_TYPE_POLYLINE2D:
            e = decode_polyline2d_r1314(hDwg, &bs, ms_end_bit, &common, data, length, &objmap);
            break;
        case DWG_R1314_TYPE_INSERT:
            e = decode_insert_r1314(hDwg, &bs, ms_end_bit, &common, data, length, &objmap);
            break;
        default: break;
        }

        if (e == NULL)
            continue;

        apply_color(e, common.color);

        {
            unsigned long layer_handle, layer_loc;
            char layer_name[DWG_R1314_MAX_LAYER_NAME];
            unsigned short layer_color = 0U;

            if (read_entity_layer_handle_r1314(&bs, ms_end_bit, &common, &layer_handle) &&
                objmap_find(&objmap, layer_handle, &layer_loc) &&
                layer_loc + 6UL < length &&
                decode_table_record_name_r1314(data, length, layer_loc, DWG_R1314_TYPE_LAYER, layer_name, sizeof(layer_name), &layer_color))
            {
                dwg_entity_put_layer(e, layer_name);

                /* see the identical comment in dwg_r2000_entity_reader.c's
                   own call site -- common.color==0/256 means BYBLOCK/
                   BYLAYER, resolve the real color from the layer instead. */
                if (common.color == 0U || common.color == 256U)
                    apply_color(e, layer_color);
            }
        }
    }

    dwg_r2000_objmap_free(&objmap);
    free(data);

    return hDwg;
}
