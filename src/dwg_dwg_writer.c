#include <stdio.h>
#include <string.h>

#include "dwg_file_io.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_layer.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_style.h"
#include "dwg_linetype.h"

/*
 * AutoCAD R12 (AC1009) binary writer.
 *
 * Byte layout follows the reverse-engineered grammar in
 * D:\estudio\DWGEngine\reverse\DWG_R12_format_reference.md (source:
 * http://www.iwriteiam.nl/DWG12.html). Several fields on that page are
 * marked uncertain ('?') or entirely undocumented (the two checksum
 * blocks, several unknown padding regions). Where we have a choice we
 * pick a fixed, always-the-same-shape encoding per entity kind (see the
 * comments on dwg_flag_for_kind below) specifically so behavior is
 * deterministic and easy to debug empirically against vecad.dll's
 * CadFileOpen, rather than trying to be maximally compact.
 *
 * Unknown/undocumented byte regions are written as zero. check_2/check_32
 * (undocumented checksums) are written as zero too -- if vecad.dll's
 * loader rejects that, CadGetError()/CadGetErrorStr() from a probe
 * harness will tell us so empirically instead of guessing further.
 */

/*
 * The 262-byte header-variables block, copied verbatim from a real R11
 * (AC1009) sample file: LibreDWG's public test suite
 * (test/test-data/r11/entities-2d.dwg), saved locally at
 * reverse/samples/r11_entities-2d.dwg (bytes 94-356). R11 and R12 share
 * the identical AC1009 format. Includes real non-degenerate extents and
 * whatever else AutoCAD itself wrote there -- ground truth instead of a
 * guess, though it's literally someone else's drawing's extents, not
 * ours (a real per-document bbox computation would be the next step).
 */
static const unsigned char dwg_r11_header_vars_template[262] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 136, 206, 101, 175, 43, 134,
    235, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 222, 221, 221, 221, 221, 221, 34, 64, 201, 253, 243, 189, 42, 65,
    34, 64, 0, 0, 0, 0, 0, 0, 20, 64, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    40, 64, 0, 0, 0, 0, 0, 0, 34, 64, 249, 20, 98, 255, 94, 218,
    24, 64, 0, 0, 0, 0, 0, 0, 18, 64, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 34, 64, 0, 0, 0, 0, 0, 0,
    0, 0, 240, 63, 0, 0, 0, 0, 0, 0, 240, 63, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 240, 63, 154, 153, 153, 153,
    153, 153, 201, 63, 154, 153, 153, 153, 153, 153, 201, 63, 0, 0, 15, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 193, 217,
    1, 227, 112, 23, 246, 63,
};

#define DWG_KIND_LINE    1
#define DWG_KIND_POINT   2
#define DWG_KIND_CIRCLE  3
#define DWG_KIND_ARC     8
#define DWG_KIND_PLINE   19
#define DWG_KIND_VERTEX  20
#define DWG_KIND_TEXT    7
#define DWG_KIND_SOLID   11
#define DWG_KIND_INSERT  14

#define LAYER_RECORD_SIZE   41UL  /* byte flag + char[32] name + word used + word color + word style + check_2 */
#define BLOCK_RECORD_SIZE   45UL  /* confirmed value from a real R11 (AC1009) sample -- see reverse/samples/ */

/*
 * STYLE (198 bytes) and LTYPE (191 bytes): both record shapes and every
 * field offset below were decoded byte-for-byte from the real STYLE/
 * LTYPE table records in reverse/samples/r11_entities-2d.dwg (a real
 * AutoCAD STANDARD style + a real CONTINUOUS linetype), cross-checked
 * against the R12 grammar's own `styles`/`ltypes` productions in
 * DWG_R12_format_reference.md (byte flag, char[32] name, word,
 * double[3], byte, double, char[128] for STYLE; byte flag, char[32]
 * name, word, char[48], byte, byte, double[13] for LTYPE) and against
 * the well-known public DXF group-code meanings for STYLE/LTYPE tables
 * (40=height, 41=width factor, 50=oblique, 71=generation flags, 3=font/
 * description, 72=alignment code, 73=dash-element count, 49=dash
 * lengths) -- three independent sources agreeing, not a guess. The
 * word right after the name field is 0xFFFF in both real records with
 * unknown semantics; copied verbatim rather than guessed at, same
 * policy as the header's own "unknown 8 bytes". Our engine doesn't
 * model a real dash-length pattern (see dwg_linetype.h) or a text
 * generation-flags bitfield (dwg_style.h has backward/upside_down as
 * separate booleans with unconfirmed real bit positions), so those
 * fields are written as 0/zero-length -- a real gap, not asserted as
 * ground truth like the rest of this record shape.
 */
#define STYLE_RECORD_SIZE  198UL
#define LTYPE_RECORD_SIZE  191UL
#define STYLE_LTYPE_WORD_MARKER 0xFFFFU

#define APPID_RECORD_SIZE   37UL  /* unused (nr=0) */
#define GENERIC_RECORD_SIZE  3UL  /* unused (nr=0): view/ucs/vport/dimstyle/p13 */

static void wr_byte(FILE *fp, unsigned char v)
{
    fputc(v, fp);
}

static void wr_bytes(FILE *fp, const void *data, unsigned long n)
{
    fwrite(data, 1, (size_t)n, fp);
}

static void wr_zeros(FILE *fp, unsigned long n)
{
    unsigned long i;

    for (i = 0UL; i < n; ++i)
        fputc(0, fp);
}

static void wr_word(FILE *fp, unsigned short v)
{
    wr_byte(fp, (unsigned char)(v & 0xFF));
    wr_byte(fp, (unsigned char)((v >> 8) & 0xFF));
}

static void wr_long(FILE *fp, unsigned long v)
{
    wr_byte(fp, (unsigned char)(v & 0xFF));
    wr_byte(fp, (unsigned char)((v >> 8) & 0xFF));
    wr_byte(fp, (unsigned char)((v >> 16) & 0xFF));
    wr_byte(fp, (unsigned char)((v >> 24) & 0xFF));
}

static void wr_double(FILE *fp, double v)
{
    /* x86 is little-endian; IEEE754 double byte order matches the format's
       expected little-endian layout directly. */
    wr_bytes(fp, &v, 8UL);
}

/* R12's 'string' type: word:length followed by that many raw bytes, no
   null terminator -- confirmed against a real R11 TEXT entity (see
   DWG_R12_format_reference.md), which encoded "FOO" as 03 00 46 4f 4f. */
static void wr_string(FILE *fp, const char *s)
{
    unsigned short len;

    len = s != NULL ? (unsigned short)strlen(s) : 0;

    wr_word(fp, len);
    if (len > 0)
        wr_bytes(fp, s, len);
}

static void wr_name32(FILE *fp, const char *name)
{
    char buf[32];

    memset(buf, 0, sizeof(buf));

    if (name != NULL)
        strncpy(buf, name, sizeof(buf) - 1UL);

    wr_bytes(fp, buf, 32UL);
}

/* Null-padded fixed-width string field, same convention as wr_name32
   generalized to any width -- used for STYLE's char[128] font field and
   LTYPE's char[48] description field. */
static void wr_fixed_string(FILE *fp, const char *s, unsigned long n)
{
    unsigned long len = (s != NULL) ? (unsigned long)strlen(s) : 0UL;

    if (len > n - 1UL)
        len = n - 1UL;
    if (len > 0UL)
        wr_bytes(fp, s, len);
    wr_zeros(fp, n - len);
}

static void wr_check2(FILE *fp)
{
    wr_zeros(fp, 2UL);
}

static void wr_check32(FILE *fp)
{
    wr_zeros(fp, 32UL);
}

static long patch_reserve(FILE *fp)
{
    long pos;

    pos = ftell(fp);
    wr_long(fp, 0UL);

    return pos;
}

static void patch_write(FILE *fp, long patch_pos, unsigned long value)
{
    long here;

    here = ftell(fp);
    fseek(fp, patch_pos, SEEK_SET);
    wr_long(fp, value);
    fseek(fp, here, SEEK_SET);
}

/*
 * Fixed per-entity encoding rules, chosen deterministically so every
 * entity of a given kind has exactly the same byte shape:
 *
 * - LINE/POINT (kind <= 2): flag bit2 (0x04) always 0, so each point is
 *   written with an explicit Z (point(TRUE) semantics) -- no shared
 *   elevation, no data loss for skew/3D lines.
 * - CIRCLE/ARC/PLINE/VERTEX (kind > 2): flag bit2 (0x04) always 1, so the
 *   common header carries one explicit elevation double, and the
 *   entity's own point(s) are 2D (X,Y only).
 * - color: flag bit0 (0x01) set only when the entity's color fits a
 *   single byte (1..255); our engine's BYLAYER-ish default (256) has no
 *   representation in this format, so it falls back to the implicit
 *   "color=0" case.
 */

static unsigned char dwg_color_flag_bit(HENTITY entity, unsigned char *out_color)
{
    unsigned short c = dwg_entity_get_color(entity);

    if (c >= 1 && c <= 255)
    {
        *out_color = (unsigned char)c;
        return 1;
    }

    *out_color = 0;
    return 0;
}

/*
 * Writes kind,flag,length,layer,opts,[color],[elevation]. 'length' is the
 * grammar's self-framing field: [length-4](...) bounds everything from
 * right after the length word through this entity's check_2, so
 * length = 4 (kind+flag+length) + layer(2) + opts(2) + color(0/1) +
 * elevation(0/8) + body_size (the kind-specific fields written by the
 * caller after this returns) + check_2(2). Getting this wrong desyncs
 * every entity after it for any reader that trusts it to skip -- this
 * was the root cause of the first crash-during-CadCountEntities probe.
 */
static void wr_entity_common_opts(FILE *fp,
                                  HENTITY entity,
                                  unsigned char kind,
                                  unsigned short layer_index,
                                  double elevation,
                                  int has_elevation,
                                  unsigned short opts,
                                  unsigned long body_size)
{
    unsigned char flag = 0;
    unsigned char color = 0;
    unsigned short color_bytes;
    unsigned short elevation_bytes;
    unsigned short length;

    if (dwg_color_flag_bit(entity, &color))
        flag |= 0x01U;

    if (has_elevation)
        flag |= 0x04U;

    color_bytes = (flag & 0x01U) ? 1U : 0U;
    elevation_bytes = has_elevation ? 8U : 0U;

    length = (unsigned short)(4UL + 2UL + 2UL + color_bytes + elevation_bytes + body_size + 2UL);

    wr_byte(fp, kind);
    wr_byte(fp, flag);
    wr_word(fp, length);

    wr_word(fp, layer_index);
    wr_word(fp, opts);

    if (flag & 0x01U)
        wr_byte(fp, color);

    if (has_elevation)
        wr_double(fp, elevation);
}

static void wr_entity_common(FILE *fp,
                             HENTITY entity,
                             unsigned char kind,
                             unsigned short layer_index,
                             double elevation,
                             int has_elevation,
                             unsigned long body_size)
{
    wr_entity_common_opts(fp, entity, kind, layer_index, elevation, has_elevation, 0, body_size);
}

static unsigned short dwg_layer_index_of(HDWG hDwg, const char *name, unsigned long layer_count)
{
    HLAYER layer;
    unsigned short idx = 0;

    if (layer_count == 0UL)
        return 0;

    if (name == NULL || name[0] == '\0')
        return 0;

    layer = dwg_document_first_layer(hDwg);
    while (layer != NULL)
    {
        if (strcmp(dwg_layer_get_name(layer), name) == 0)
            return idx;

        layer = dwg_document_next_layer(layer);
        idx++;
    }

    return 0; /* not found: fall back to the first layer */
}

static unsigned short dwg_block_index_of(HDWG hDwg, const char *name)
{
    HBLOCK block;
    unsigned short idx = 0;

    if (name == NULL || name[0] == '\0')
        return 0;

    block = dwg_document_first_block(hDwg);
    while (block != NULL)
    {
        if (strcmp(dwg_block_get_name(block), name) == 0)
            return idx;

        block = dwg_document_next_block(block);
        idx++;
    }

    return 0; /* not found: fall back to the first block */
}

/*
 * TEXT (kind 7): confirmed against a real R11 sample (entity #4 in
 * DWG_R12_format_reference.md) -- point(FALSE) with shared elevation,
 * height, length-prefixed string, then (since we always set opts bits
 * 0x20+0x40, matching what the real sample itself did) a byte alignment
 * code and a second point(FALSE) for point0, sharing the same elevation.
 */
static void wr_text(FILE *fp, HENTITY entity, unsigned short layer_index)
{
    double x, y, z, x0, y0, z0;
    unsigned long body_size;
    const char *text;

    dwg_text_get_point(entity, &x, &y, &z);
    dwg_text_get_point0(entity, &x0, &y0, &z0);
    text = dwg_text_get_text(entity);
    if (text == NULL) text = "";

    body_size = 16UL + 8UL + 2UL + (unsigned long)strlen(text) + 1UL + 16UL;

    wr_entity_common_opts(fp, entity, DWG_KIND_TEXT, layer_index, z, 1, 0x0060U, body_size);

    wr_double(fp, x);
    wr_double(fp, y);
    wr_double(fp, dwg_text_get_height(entity));
    wr_string(fp, text);
    wr_byte(fp, (unsigned char)dwg_text_get_align(entity));
    wr_double(fp, x0);
    wr_double(fp, y0);

    wr_check2(fp);
}

/*
 * SOLID (kind 11): confirmed against a real R11 sample (entity #11 in
 * DWG_R12_format_reference.md) -- 4 points, each point(FALSE) (2D, x,y)
 * sharing one elevation double for all four z's, matching the R12
 * grammar's TRACE/SOLID/3DFACE shape exactly. Real points decoded:
 * (7,1),(8,2),(7,3),(8,4) at elevation 2.0.
 */
static void wr_solid(FILE *fp, HENTITY entity, const DWG_SOLID3D *s, unsigned short layer_index)
{
    wr_entity_common(fp, entity, DWG_KIND_SOLID, layer_index, s->p1.z, 1, 64UL); /* 8 doubles (4 x,y pairs) */

    wr_double(fp, s->p1.x); wr_double(fp, s->p1.y);
    wr_double(fp, s->p2.x); wr_double(fp, s->p2.y);
    wr_double(fp, s->p3.x); wr_double(fp, s->p3.y);
    wr_double(fp, s->p4.x); wr_double(fp, s->p4.y);

    wr_check2(fp);
}

/*
 * INSERT (kind 14): confirmed against a real R11 sample (see
 * DWG_R12_format_reference.md) -- word block-table-index, point(FALSE)
 * with shared elevation, then (opts always 0xF here, matching what the
 * real sample itself set) scale x/y/z and rotation angle as doubles, in
 * standard DXF group-code order (41/42/43/50). Which of the last two
 * real fields is z-scale vs. rotation wasn't 100% distinguishable from
 * a single real sample -- see the reference doc.
 */
static void wr_insert(FILE *fp, HDWG hDwg, HENTITY entity, unsigned short layer_index)
{
    double x, y, z, sx, sy, sz;
    unsigned short block_idx;

    dwg_insert_get_point(entity, &x, &y, &z);
    dwg_insert_get_scale(entity, &sx, &sy, &sz);
    block_idx = dwg_block_index_of(hDwg, dwg_insert_get_block_name(entity));

    wr_entity_common_opts(fp, entity, DWG_KIND_INSERT, layer_index, z, 1, 0x000FU, 50UL);

    wr_word(fp, block_idx);
    wr_double(fp, x);
    wr_double(fp, y);
    wr_double(fp, sx);
    wr_double(fp, sy);
    wr_double(fp, sz);
    wr_double(fp, dwg_insert_get_angle(entity));

    wr_check2(fp);
}

/* STYLE table record (198 bytes) -- field layout confirmed from a real
   sample, see the STYLE_RECORD_SIZE comment above for sourcing. */
static void wr_style_record(FILE *fp, HSTYLE style)
{
    wr_byte(fp, 0);                                    /* flag */
    wr_name32(fp, dwg_style_get_name(style));
    wr_word(fp, (unsigned short)STYLE_LTYPE_WORD_MARKER);
    wr_double(fp, dwg_style_get_height(style));         /* DXF 40 */
    wr_double(fp, dwg_style_get_width_factor(style));    /* DXF 41 */
    wr_double(fp, dwg_style_get_oblique(style));         /* DXF 50 */
    wr_byte(fp, 0);                                    /* DXF 71 generation flags: not modeled, see comment above */
    wr_double(fp, dwg_style_get_height(style));          /* DXF 42 last height used: no separate field, reuse height */
    wr_fixed_string(fp, dwg_style_get_font(style), 128UL); /* DXF 3 */
    wr_check2(fp);
}

/* LTYPE table record (191 bytes) -- field layout confirmed from a real
   sample, see the STYLE_RECORD_SIZE comment above for sourcing. */
static void wr_ltype_record(FILE *fp, HLINETYPE linetype)
{
    int i;

    wr_byte(fp, 0);                                    /* flag */
    wr_name32(fp, dwg_linetype_get_name(linetype));
    wr_word(fp, (unsigned short)STYLE_LTYPE_WORD_MARKER);
    wr_fixed_string(fp, dwg_linetype_get_descr(linetype), 48UL); /* DXF 3 */
    wr_byte(fp, (unsigned char)'A');                    /* DXF 72 alignment code: always 'A' in real files */
    wr_byte(fp, 0);                                     /* DXF 73 dash-element count: no pattern modeled, see comment above */
    for (i = 0; i < 13; ++i)
        wr_double(fp, 0.0);                             /* DXF 40/49 total length + dash lengths: no pattern modeled */
    wr_check2(fp);
}

static void wr_line(FILE *fp, HENTITY entity, const DWG_LINE3D *l, unsigned short layer_index)
{
    wr_entity_common(fp, entity, DWG_KIND_LINE, layer_index, 0.0, 0, 48UL); /* 6 doubles */

    wr_double(fp, l->start.x);
    wr_double(fp, l->start.y);
    wr_double(fp, l->start.z);

    wr_double(fp, l->end.x);
    wr_double(fp, l->end.y);
    wr_double(fp, l->end.z);

    wr_check2(fp);
}

static void wr_point(FILE *fp, HENTITY entity, const DWG_POINT3D *p, unsigned short layer_index)
{
    wr_entity_common(fp, entity, DWG_KIND_POINT, layer_index, 0.0, 0, 24UL); /* 3 doubles */

    wr_double(fp, p->x);
    wr_double(fp, p->y);
    wr_double(fp, p->z);

    wr_check2(fp);
}

static void wr_circle(FILE *fp, HENTITY entity, const DWG_CIRCLE3D *c, unsigned short layer_index)
{
    wr_entity_common(fp, entity, DWG_KIND_CIRCLE, layer_index, c->center.z, 1, 24UL); /* 2 doubles + radius */

    wr_double(fp, c->center.x);
    wr_double(fp, c->center.y);
    wr_double(fp, c->radius);

    wr_check2(fp);
}

static void wr_arc(FILE *fp, HENTITY entity, const DWG_ARC3D *a, unsigned short layer_index)
{
    wr_entity_common(fp, entity, DWG_KIND_ARC, layer_index, a->center.z, 1, 40UL); /* 2 doubles + radius+start+end */

    wr_double(fp, a->center.x);
    wr_double(fp, a->center.y);
    wr_double(fp, a->radius);
    wr_double(fp, a->start_angle);
    wr_double(fp, a->end_angle);

    wr_check2(fp);
}

static void wr_polyline(FILE *fp, HENTITY entity, unsigned short layer_index)
{
    HPOLYLINE polyline;
    HVERTEX vertex;
    unsigned char closed_byte;

    polyline = dwg_polyline_from_entity(entity);
    if (polyline == NULL)
        return;

    closed_byte = (unsigned char)(dwg_polyline_is_closed(polyline) ? 1 : 0);

    /* opts bit0 (0x01) = "has l70" (our closed-flag byte) */
    wr_entity_common_opts(fp, entity, DWG_KIND_PLINE, layer_index,
                          dwg_polyline_get_elevation(polyline), 1, 0x0001U, 1UL);
    wr_byte(fp, closed_byte);
    wr_check2(fp);

    vertex = dwg_polyline_first_vertex(polyline);
    while (vertex != NULL)
    {
        double x, y, z, bulge;

        dwg_vertex_get_point(vertex, &x, &y, &z);
        bulge = dwg_vertex_get_bulge(vertex);

        /* opts bit3 (0x08) = "has l50" (our bulge double -- '?' on the
           source grammar, best-effort placement, verify empirically) */
        wr_entity_common_opts(fp, entity, DWG_KIND_VERTEX, layer_index, z, 1, 0x0008U, 24UL);
        wr_double(fp, x);
        wr_double(fp, y);
        wr_double(fp, bulge);
        wr_check2(fp);

        vertex = dwg_polyline_next_vertex(vertex);
    }
}

DWG_IO_RESULT dwg_write_dwg_r12(HDWG hDwg, const char *path)
{
    FILE *fp;
    HENTITY entity;
    HLAYER layer;
    unsigned long layer_count;

    long pos_p_entities, pos_p_entend;
    long pos_p_blocksec, pos_p_bsend;
    long pos_block_start, pos_layer_start, pos_style_start;
    long pos_ltype_start, pos_view_start, pos_ucs_start;
    long pos_vport_start, pos_appid_start, pos_dimstyle_start, pos_p13_start;

    unsigned long v_entities, v_entend;
    unsigned long v_blocksec, v_bsend;
    unsigned long v_block_start, v_layer_start, v_style_start;
    unsigned long v_ltype_start, v_view_start, v_ucs_start;
    unsigned long v_vport_start, v_appid_start, v_dimstyle_start, v_p13_start;

    if (hDwg == NULL || path == NULL)
        return DWG_IO_ERROR_OPEN;

    fp = fopen(path, "wb");
    if (fp == NULL)
        return DWG_IO_ERROR_OPEN;

    layer_count = dwg_document_layer_count(hDwg);
    if (layer_count == 0UL)
        layer_count = 1UL; /* implicit layer "0", see dwg_layer_index_of */

    /* ---- file header ---- */

    wr_bytes(fp, "AC1009\0\0\0\0\0\0", 12UL);

    /* byte,word,word,word,byte -- undocumented on the source grammar page,
       but confirmed byte-for-byte from a real R11 (AC1009) sample file
       (reverse/samples/r11_entities-2d.dwg, LibreDWG's public test data):
       01 03 00 05 00 cd 00 00. Meaning still unknown; copied verbatim
       rather than left zero since it's clearly not zero in a real file. */
    wr_bytes(fp, "\x01\x03\x00\x05\x00\xcd\x00\x00", 8UL);

    pos_p_entities = patch_reserve(fp);
    pos_p_entend   = patch_reserve(fp);

    pos_p_blocksec = patch_reserve(fp);
    wr_zeros(fp, 4UL);
    pos_p_bsend    = patch_reserve(fp);
    wr_zeros(fp, 4UL);

    wr_word(fp, (unsigned short)BLOCK_RECORD_SIZE);
    wr_long(fp, dwg_document_block_count(hDwg));
    pos_block_start = patch_reserve(fp);

    wr_word(fp, (unsigned short)LAYER_RECORD_SIZE);
    wr_long(fp, layer_count);
    pos_layer_start = patch_reserve(fp);

    wr_word(fp, (unsigned short)STYLE_RECORD_SIZE);
    wr_long(fp, dwg_document_style_count(hDwg));
    pos_style_start = patch_reserve(fp);

    wr_word(fp, (unsigned short)LTYPE_RECORD_SIZE);
    wr_long(fp, dwg_document_linetype_count(hDwg));
    pos_ltype_start = patch_reserve(fp);

    wr_word(fp, (unsigned short)GENERIC_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_view_start = patch_reserve(fp);

    /* header variables block (262 bytes). Written verbatim from a real
       R11 (AC1009) sample file (LibreDWG's public test data,
       reverse/samples/r11_entities-2d.dwg, bytes 94-356) rather than
       guessed -- includes real (non-degenerate) extents/limits and
       whatever else AutoCAD itself put there. Using someone else's
       actual extents on our own documents is a known simplification
       (worth replacing with our own computed bbox later); the point
       here was ruling in/out real vs. guessed defaults for the hang. */
    wr_bytes(fp, dwg_r11_header_vars_template, 262UL);

    /* pad to absolute offset 0x3EF */
    wr_zeros(fp, (unsigned long)(0x3EF - ftell(fp)));

    wr_word(fp, (unsigned short)GENERIC_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_ucs_start = patch_reserve(fp);

    /* pad to absolute offset 0x500 */
    wr_zeros(fp, (unsigned long)(0x500 - ftell(fp)));

    wr_word(fp, (unsigned short)GENERIC_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_vport_start = patch_reserve(fp);

    wr_zeros(fp, 8UL);

    wr_word(fp, (unsigned short)APPID_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_appid_start = patch_reserve(fp);

    wr_zeros(fp, 6UL);

    wr_word(fp, (unsigned short)GENERIC_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_dimstyle_start = patch_reserve(fp);

    /* pad to absolute offset 0x69F */
    wr_zeros(fp, (unsigned long)(0x69F - ftell(fp)));

    wr_word(fp, (unsigned short)GENERIC_RECORD_SIZE);
    wr_long(fp, 0UL);
    pos_p13_start = patch_reserve(fp);

    wr_zeros(fp, 38UL);

    /* ---- entities section ---- */

    v_entities = (unsigned long)ftell(fp);

    entity = dwg_document_first_entity(hDwg);
    while (entity != NULL)
    {
        unsigned short layer_index;

        layer_index = dwg_layer_index_of(hDwg, dwg_entity_get_layer(entity), layer_count);

        switch (dwg_entity_get_type(entity))
        {
        case DWG_ENTITY_LINE:
            wr_line(fp, entity, (const DWG_LINE3D *)entity->geometry, layer_index);
            break;

        case DWG_ENTITY_POINT:
            wr_point(fp, entity, (const DWG_POINT3D *)entity->geometry, layer_index);
            break;

        case DWG_ENTITY_CIRCLE:
            wr_circle(fp, entity, (const DWG_CIRCLE3D *)entity->geometry, layer_index);
            break;

        case DWG_ENTITY_ARC:
            wr_arc(fp, entity, (const DWG_ARC3D *)entity->geometry, layer_index);
            break;

        case DWG_ENTITY_POLYLINE:
            wr_polyline(fp, entity, layer_index);
            break;

        case DWG_ENTITY_TEXT:
            wr_text(fp, entity, layer_index);
            break;

        case DWG_ENTITY_SOLID:
            wr_solid(fp, entity, (const DWG_SOLID3D *)entity->geometry, layer_index);
            break;

        case DWG_ENTITY_INSERT:
            wr_insert(fp, hDwg, entity, layer_index);
            break;

        default:
            break; /* not modeled yet: silently skipped, same as the DXF writer */
        }

        entity = dwg_document_next_entity(entity);
    }

    v_entend = (unsigned long)ftell(fp);

    wr_zeros(fp, 19UL); /* byte[19] after entities -- undocumented on source page */

    /* ---- tables (block/layer/style/ltype/view/ucs/vport/appid/dimstyle/p13) ---- */

    v_block_start = (unsigned long)ftell(fp);
    {
        HBLOCK block = dwg_document_first_block(hDwg);

        while (block != NULL)
        {
            /* Record shape confirmed 45 bytes total from a real R11
               sample (see reverse/samples/); grammar-derived fields
               only account for 43, so 2 bytes of unknown padding are
               included here to match the confirmed real size exactly. */
            wr_byte(fp, 0);                    /* flag */
            wr_name32(fp, dwg_block_get_name(block));
            wr_word(fp, 0);                    /* used */
            wr_byte(fp, 0);
            wr_word(fp, 0);
            wr_byte(fp, 0);
            wr_word(fp, 0);
            wr_word(fp, 0);                    /* unknown, see comment above */
            wr_check2(fp);

            block = dwg_document_next_block(block);
        }
    }
    wr_check32(fp);

    v_layer_start = (unsigned long)ftell(fp);
    if (dwg_document_layer_count(hDwg) == 0UL)
    {
        wr_byte(fp, 0);
        wr_name32(fp, "0");
        wr_word(fp, 0);
        wr_word(fp, 7);   /* default color, matches dwg_layer_create's default */
        wr_word(fp, 0);
        wr_check2(fp);
    }
    else
    {
        layer = dwg_document_first_layer(hDwg);
        while (layer != NULL)
        {
            wr_byte(fp, 0);
            wr_name32(fp, dwg_layer_get_name(layer));
            wr_word(fp, 0);
            wr_word(fp, dwg_layer_get_color(layer));
            wr_word(fp, 0);
            wr_check2(fp);

            layer = dwg_document_next_layer(layer);
        }
    }
    wr_check32(fp);

    v_style_start = (unsigned long)ftell(fp);
    {
        HSTYLE style = dwg_document_first_style(hDwg);
        while (style != NULL)
        {
            wr_style_record(fp, style);
            style = dwg_document_next_style(style);
        }
    }
    wr_check32(fp);

    v_ltype_start = (unsigned long)ftell(fp);
    {
        HLINETYPE linetype = dwg_document_first_linetype(hDwg);
        while (linetype != NULL)
        {
            wr_ltype_record(fp, linetype);
            linetype = dwg_document_next_linetype(linetype);
        }
    }
    wr_check32(fp);

    v_view_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    v_ucs_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    v_vport_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    v_appid_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    v_dimstyle_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    v_p13_start = (unsigned long)ftell(fp);
    wr_check32(fp);

    /* ---- block-definition entities section (empty: no BLOCK/INSERT support yet) ---- */

    v_blocksec = (unsigned long)ftell(fp);
    v_bsend = v_blocksec;

    wr_zeros(fp, 36UL); /* bytes[36] after block entities -- undocumented on source page */

    /* ---- trailer: repeats every pointer as a consistency check ---- */

    wr_long(fp, v_entities);
    wr_long(fp, v_entend);
    wr_long(fp, v_blocksec);
    wr_long(fp, v_bsend);
    wr_zeros(fp, 12UL);
    wr_zeros(fp, 6UL);
    wr_long(fp, v_block_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_layer_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_style_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_ltype_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_view_start);      wr_zeros(fp, 6UL);
    wr_long(fp, v_ucs_start);       wr_zeros(fp, 6UL);
    wr_long(fp, v_vport_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_appid_start);     wr_zeros(fp, 6UL);
    wr_long(fp, v_dimstyle_start);  wr_zeros(fp, 6UL);
    wr_long(fp, v_p13_start);       wr_zeros(fp, 6UL);
    wr_long(fp, 0UL); /* trailing longp; no further bytes* written */

    /* ---- backpatch the early placeholders now that every value is known ---- */

    patch_write(fp, pos_p_entities, v_entities);
    patch_write(fp, pos_p_entend, v_entend);
    patch_write(fp, pos_p_blocksec, v_blocksec);
    patch_write(fp, pos_p_bsend, v_bsend);
    patch_write(fp, pos_block_start, v_block_start);
    patch_write(fp, pos_layer_start, v_layer_start);
    patch_write(fp, pos_style_start, v_style_start);
    patch_write(fp, pos_ltype_start, v_ltype_start);
    patch_write(fp, pos_view_start, v_view_start);
    patch_write(fp, pos_ucs_start, v_ucs_start);
    patch_write(fp, pos_vport_start, v_vport_start);
    patch_write(fp, pos_appid_start, v_appid_start);
    patch_write(fp, pos_dimstyle_start, v_dimstyle_start);
    patch_write(fp, pos_p13_start, v_p13_start);

    fclose(fp);

    return DWG_IO_OK;
}
