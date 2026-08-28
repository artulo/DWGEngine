#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dwg_r2000_writer.h"
#include "dwg_r2000_reader.h"
#include "dwg_bitstream.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_solid.h"
#include "dwg_insert.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * R2000 (AC1015) writer -- "template + injection" architecture.
 * Ported field-by-field from the validated Python prototype
 * (reverse/dwg_r2000_writer_prototype.py + dwg_r2000_bitwriter.py) --
 * see reverse/DWG_R2000_writer_plan.md for the architecture and every
 * real bug found while deriving the field orders/CRC recipes/obj_size
 * convention this file relies on. Not re-deriving any of that here;
 * this is a straight port, cross-checked against the Python output by
 * running both writers on the same input and diffing (see
 * tests/test_r2000_writer_c.c).
 */

#define DWG_R2000_TYPE_TEXT       0x01
#define DWG_R2000_TYPE_INSERT     0x07
#define DWG_R2000_TYPE_VERTEX2D   0x0A
#define DWG_R2000_TYPE_VERTEX3D   0x0B
#define DWG_R2000_TYPE_SEQEND     0x06
#define DWG_R2000_TYPE_POLYLINE2D 0x0F
#define DWG_R2000_TYPE_POLYLINE3D 0x10
#define DWG_R2000_TYPE_ARC        0x11
#define DWG_R2000_TYPE_CIRCLE     0x12
#define DWG_R2000_TYPE_LINE       0x13
#define DWG_R2000_TYPE_POINT      0x1B
#define DWG_R2000_TYPE_SOLID      0x1F
#define DWG_R2000_TYPE_MTEXT      0x2C
#define DWG_R2000_TYPE_BLOCK_HEADER 0x31
#define DWG_R2000_TYPE_LAYER      0x33
#define DWG_R2000_TYPE_STYLE      0x35

/* ================= growable bit writer ================= */

typedef struct
{
    unsigned char *data;
    unsigned long capacity;  /* bytes allocated */
    unsigned long bit_count; /* bits written so far */
    int failed;              /* set once a growth realloc fails; sticky for the writer's lifetime */
} DWG_BW;

static void bw_init(DWG_BW *bw)
{
    bw->data = NULL;
    bw->capacity = 0UL;
    bw->bit_count = 0UL;
    bw->failed = 0;
}

static void bw_free(DWG_BW *bw)
{
    if (bw->data != NULL)
        free(bw->data);
    bw->data = NULL;
    bw->capacity = 0UL;
    bw->bit_count = 0UL;
}

static void bw_ensure(DWG_BW *bw, unsigned long need_bytes)
{
    unsigned long newcap;
    unsigned char *newdata;

    if (need_bytes <= bw->capacity)
        return;

    newcap = bw->capacity ? bw->capacity * 2UL : 64UL;
    if (newcap < need_bytes)
        newcap = need_bytes;

    newdata = (unsigned char *)realloc(bw->data, newcap);
    if (newdata == NULL)
    {
        /* bw->data/capacity are left exactly as before (realloc never
           frees the old block on failure) -- but that old block may
           now be too small for the write already in flight, so mark
           this writer permanently failed rather than let bw_write_bit
           silently write within the old (too-small) capacity, which
           would be an out-of-bounds heap write the moment byte_i
           reaches/exceeds the untouched old capacity. */
        bw->failed = 1;
        return;
    }

    memset(newdata + bw->capacity, 0, newcap - bw->capacity);
    bw->data = newdata;
    bw->capacity = newcap;
}

static unsigned long bw_tell_bit(DWG_BW *bw)
{
    return bw->bit_count;
}

static void bw_write_bit(DWG_BW *bw, unsigned long v)
{
    unsigned long byte_i = bw->bit_count / 8UL;
    unsigned long bit_i = 7UL - (bw->bit_count % 8UL);

    bw_ensure(bw, byte_i + 1UL);
    if (bw->data == NULL || bw->failed)
        return;
    if (v)
        bw->data[byte_i] |= (unsigned char)(1U << bit_i);
    bw->bit_count++;
}

static void bw_write_bits(DWG_BW *bw, unsigned long v, unsigned long n)
{
    long i;

    for (i = (long)n - 1L; i >= 0L; i--)
        bw_write_bit(bw, (v >> i) & 1UL);
}

static void bw_write_raw_le_bytes(DWG_BW *bw, const unsigned char *bytes, unsigned long nbytes)
{
    unsigned long i;

    for (i = 0UL; i < nbytes; i++)
        bw_write_bits(bw, bytes[i], 8UL);
}

static void bw_write_rc(DWG_BW *bw, unsigned char v)
{
    bw_write_bits(bw, v, 8UL);
}

static void bw_write_rl(DWG_BW *bw, unsigned long v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFUL);
    b[1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[3] = (unsigned char)((v >> 24) & 0xFFUL);
    bw_write_raw_le_bytes(bw, b, 4UL);
}

static void bw_write_rd(DWG_BW *bw, double v)
{
    unsigned char b[8];
    memcpy(b, &v, 8UL); /* x86 little-endian, matches dwg_bs_read_rd's own assumption */
    bw_write_raw_le_bytes(bw, b, 8UL);
}

static void bw_write_bb(DWG_BW *bw, unsigned long v)
{
    bw_write_bits(bw, v & 0x3UL, 2UL);
}

static void bw_write_bs(DWG_BW *bw, unsigned long v)
{
    if (v == 0UL)
        bw_write_bb(bw, 2UL);
    else if (v == 256UL)
        bw_write_bb(bw, 3UL);
    else if (v <= 255UL)
    {
        bw_write_bb(bw, 1UL);
        bw_write_bits(bw, v, 8UL);
    }
    else
    {
        unsigned char b[2];
        bw_write_bb(bw, 0UL);
        b[0] = (unsigned char)(v & 0xFFUL);
        b[1] = (unsigned char)((v >> 8) & 0xFFUL);
        bw_write_raw_le_bytes(bw, b, 2UL);
    }
}

static void bw_write_bl(DWG_BW *bw, unsigned long v)
{
    if (v == 0UL)
        bw_write_bb(bw, 2UL);
    else if (v <= 255UL)
    {
        bw_write_bb(bw, 1UL);
        bw_write_bits(bw, v, 8UL);
    }
    else
    {
        bw_write_bb(bw, 0UL);
        bw_write_rl(bw, v);
    }
}

static void bw_write_bd(DWG_BW *bw, double v)
{
    if (v == 0.0)
        bw_write_bb(bw, 2UL);
    else if (v == 1.0)
        bw_write_bb(bw, 1UL);
    else
    {
        bw_write_bb(bw, 0UL);
        bw_write_rd(bw, v);
    }
}

static void bw_write_3bd(DWG_BW *bw, double x, double y, double z)
{
    bw_write_bd(bw, x);
    bw_write_bd(bw, y);
    bw_write_bd(bw, z);
}

static void bw_write_be(DWG_BW *bw, double x, double y, double z)
{
    if (x == 0.0 && y == 0.0 && z == 1.0)
        bw_write_bit(bw, 1UL);
    else
    {
        bw_write_bit(bw, 0UL);
        bw_write_3bd(bw, x, y, z);
    }
}

static void bw_write_bt(DWG_BW *bw, double v)
{
    if (v == 0.0)
        bw_write_bit(bw, 1UL);
    else
    {
        bw_write_bit(bw, 0UL);
        bw_write_bd(bw, v);
    }
}

/* DD: patches the cheapest byte-slot of `default`'s packed bytes that
   exactly reproduces `value`, matching dwg_bs_read_dd's decode rule --
   see the equivalent Python write_dd for the full rationale (1.0 vs
   2.0 already differ in the top bytes, so code1 alone can't represent
   every "close" pair -- must fall back to a full raw double whenever
   the cheaper slots can't reproduce the value exactly bit for bit). */
static void bw_write_dd(DWG_BW *bw, double default_value, double value)
{
    unsigned char pv[8], pd[8];

    if (value == default_value)
    {
        bw_write_bb(bw, 0UL);
        return;
    }

    memcpy(pv, &value, 8UL);
    memcpy(pd, &default_value, 8UL);

    if (memcmp(pv + 4, pd + 4, 4UL) == 0)
    {
        bw_write_bb(bw, 1UL);
        bw_write_raw_le_bytes(bw, pv, 4UL);
    }
    else if (memcmp(pv + 6, pd + 6, 2UL) == 0)
    {
        bw_write_bb(bw, 2UL);
        bw_write_raw_le_bytes(bw, pv + 4, 2UL);
        bw_write_raw_le_bytes(bw, pv, 4UL);
    }
    else
    {
        bw_write_bb(bw, 3UL);
        bw_write_rd(bw, value);
    }
}

/* MC: unsigned (hoff) uses plain 7-bit groups (full 7 payload bits in
   every byte, including the last). Signed (loff, and every handle
   count byte) reserves the FINAL byte's 0x40 bit for the sign flag,
   so that byte can only carry 6 magnitude bits -- get this wrong and
   values like 127 round-trip as -63 (caught by a synthetic test
   before this ever touched real data, see the Python bitwriter's own
   comment). */
static void bw_write_mc(DWG_BW *bw, long value, int signed_allowed)
{
    unsigned long v;
    int negative;
    unsigned char out[8];
    unsigned long n = 0UL;
    unsigned long k;

    negative = signed_allowed && value < 0L;
    v = (unsigned long)(negative ? -value : value);

    if (signed_allowed)
    {
        /* Counts continuation bytes needed beyond the final 6-bit-
           payload byte via repeated RIGHT shifts of a scratch copy,
           not by growing a left-shifted comparison threshold -- a
           left shift of (7*k+6) bits is undefined behavior in C once
           it reaches/exceeds unsigned long's width (32 bits here,
           reachable for k>=4, i.e. any delta >= 2^27), which a large
           template file's object-map location deltas can realistically
           hit (pairs are sorted by handle, not location, so deltas
           aren't bounded by anything small). Right-shifting a copy by
           a fixed 7 bits per iteration never approaches the type's
           width, so this is well-defined for the full range of a
           32-bit value. */
        unsigned long remaining = v >> 6;
        k = 0UL;
        while (remaining != 0UL)
        {
            remaining >>= 7;
            k++;
        }
        for (n = 0UL; n < k; n++)
            out[n] = (unsigned char)(((v >> (7UL * n)) & 0x7FUL) | 0x80UL);
        out[n] = (unsigned char)((v >> (7UL * k)) & 0x3FUL);
        if (negative)
            out[n] |= 0x40U;
        n++;
    }
    else
    {
        unsigned long rem = v;
        for (;;)
        {
            unsigned char payload = (unsigned char)(rem & 0x7FUL);
            rem >>= 7;
            if (rem == 0UL)
            {
                out[n++] = payload;
                break;
            }
            out[n++] = (unsigned char)(payload | 0x80U);
        }
    }

    bw_write_raw_le_bytes(bw, out, n);
}

static void bw_write_ms(DWG_BW *bw, unsigned long value)
{
    unsigned short shorts[4];
    unsigned long n = 0UL;
    unsigned long v = value;

    for (;;)
    {
        unsigned short payload = (unsigned short)(v & 0x7FFFUL);
        v >>= 15;
        if (v == 0UL)
        {
            shorts[n++] = payload;
            break;
        }
        shorts[n++] = (unsigned short)(payload | 0x8000U);
    }

    {
        unsigned long i;
        for (i = 0UL; i < n; i++)
        {
            unsigned char b[2];
            b[0] = (unsigned char)(shorts[i] & 0xFFU);
            b[1] = (unsigned char)((shorts[i] >> 8) & 0xFFU);
            bw_write_raw_le_bytes(bw, b, 2UL);
        }
    }
}

static void bw_write_handle(DWG_BW *bw, unsigned char code, unsigned long value)
{
    unsigned long nbytes;
    long i;

    nbytes = 0UL;
    {
        unsigned long tmp = value;
        while (tmp != 0UL)
        {
            nbytes++;
            tmp >>= 8;
        }
    }

    bw_write_bits(bw, (((unsigned long)code & 0xFUL) << 4) | (nbytes & 0xFUL), 8UL);
    for (i = (long)nbytes - 1L; i >= 0L; i--)
        bw_write_bits(bw, (value >> (8UL * (unsigned long)i)) & 0xFFUL, 8UL);
}

static void bw_write_t(DWG_BW *bw, const char *s)
{
    unsigned long len = (unsigned long)strlen(s);
    unsigned long i;

    bw_write_bs(bw, len);
    for (i = 0UL; i < len; i++)
        bw_write_bits(bw, (unsigned char)s[i], 8UL);
}

/* Table "Entry name" fields (LAYER/STYLE/BLOCK_HEADER) are a real,
   confirmed exception to T's usual no-null convention: their declared
   length INCLUDES a trailing null byte (see
   reverse/DWG_R2000_format_reference.md's LAYER-resolution entry).
   Matters for byte-for-byte real-file fidelity (Arturo's explicit
   writer goal), not just self-consistency with this engine's own
   reader (which tolerates either shape). */
static void bw_write_entry_name(DWG_BW *bw, const char *s)
{
    unsigned long len = (unsigned long)strlen(s);
    unsigned long i;

    bw_write_bs(bw, len + 1UL);
    for (i = 0UL; i < len; i++)
        bw_write_bits(bw, (unsigned char)s[i], 8UL);
    bw_write_bits(bw, 0U, 8UL);
}

static void bw_align_to_byte(DWG_BW *bw)
{
    while (bw->bit_count % 8UL != 0UL)
        bw_write_bit(bw, 0UL);
}

/* ================= CRC (seed 0xC0C1, confirmed empirically -- see
   the reference doc's "Object-trailer CRC algorithm" entry) ================= */

static const unsigned short dwg_r2000_crc_table[256] = {
0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,
0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040,
};

static unsigned short dwg_r2000_crc(unsigned short seed, const unsigned char *buf, unsigned long n)
{
    unsigned short s = seed;
    unsigned long i;

    for (i = 0UL; i < n; i++)
    {
        unsigned char al = (unsigned char)(buf[i] ^ (s & 0xFFU));
        s = (unsigned short)((s >> 8) & 0xFFU);
        s = (unsigned short)(s ^ dwg_r2000_crc_table[al]);
    }
    return s;
}

/* ================= object body assembly (two-pass obj_size) =================
 *
 * obj_size is measured from right after the MS Length field (so it
 * INCLUDES Type/RL-itself/Handle/EED-size) and STOPS right before the
 * handle stream (fields_fn's own span only) -- confirmed empirically
 * against a real LINE, see the reference doc and the Python
 * bitwriter's own encode_object_body comment for how this was
 * originally gotten backwards and caught.
 */

typedef void (*DWG_R2000_WRITE_FN)(DWG_BW *bw, const void *ctx);

static unsigned char *r2000_encode_object(unsigned short type_code, unsigned long handle,
                                          DWG_R2000_WRITE_FN fields_fn,
                                          DWG_R2000_WRITE_FN handles_fn,
                                          const void *ctx, unsigned long *out_len)
{
    DWG_BW probe, body;
    unsigned long obj_size_bits;
    unsigned long length, ms_len_bytes;
    unsigned char *full;
    unsigned short crc;

    bw_init(&probe);
    bw_write_bs(&probe, type_code);
    bw_write_rl(&probe, 0UL);
    bw_write_handle(&probe, 0, handle);
    bw_write_bs(&probe, 0UL);
    fields_fn(&probe, ctx);
    obj_size_bits = bw_tell_bit(&probe);
    if (probe.failed)
    {
        bw_free(&probe);
        *out_len = 0UL;
        return NULL;
    }
    bw_free(&probe);

    bw_init(&body);
    bw_write_bs(&body, type_code);
    bw_write_rl(&body, obj_size_bits);
    bw_write_handle(&body, 0, handle);
    bw_write_bs(&body, 0UL);
    fields_fn(&body, ctx);
    handles_fn(&body, ctx);
    bw_align_to_byte(&body);

    if (body.data == NULL || body.failed)
    {
        bw_free(&body);
        *out_len = 0UL;
        return NULL;
    }

    length = body.bit_count / 8UL;

    /* MS length prefix, byte-aligned on its own */
    {
        DWG_BW ms;
        bw_init(&ms);
        bw_write_ms(&ms, length);
        bw_align_to_byte(&ms);
        if (ms.data == NULL || ms.failed)
        {
            bw_free(&ms);
            bw_free(&body);
            *out_len = 0UL;
            return NULL;
        }
        ms_len_bytes = ms.bit_count / 8UL;

        full = (unsigned char *)malloc(ms_len_bytes + length + 2UL);
        if (full == NULL)
        {
            bw_free(&ms);
            bw_free(&body);
            *out_len = 0UL;
            return NULL;
        }
        memcpy(full, ms.data, ms_len_bytes);
        memcpy(full + ms_len_bytes, body.data, length);
        bw_free(&ms);
    }
    bw_free(&body);

    crc = dwg_r2000_crc(0xC0C1U, full, ms_len_bytes + length);
    full[ms_len_bytes + length] = (unsigned char)(crc & 0xFFU);
    full[ms_len_bytes + length + 1UL] = (unsigned char)((crc >> 8) & 0xFFU);

    *out_len = ms_len_bytes + length + 2UL;
    return full;
}

/* ================= common entity data/handle helpers ================= */

typedef struct
{
    unsigned short color;
    double ltscale;
    unsigned long ltflags;
    unsigned long plotflags;
    unsigned short invisibility;
    unsigned char lineweight;
    unsigned long entmode; /* 2 = implicit Model_Space owner (top-level entities);
                               0 = explicit owner ref (VERTEX/SEQEND owned by a POLYLINE) */
} DWG_R2000_COMMON_W;

static void common_w_defaults(DWG_R2000_COMMON_W *c)
{
    c->color = 256U; /* BYLAYER */
    c->ltscale = 1.0;
    c->ltflags = 0UL;
    c->plotflags = 0UL;
    c->invisibility = 0U;
    c->lineweight = 29U;
    c->entmode = 2UL;
}

static void write_common_entity_fields(DWG_BW *bw, const DWG_R2000_COMMON_W *c)
{
    bw_write_bit(bw, 0UL); /* graphic present flag */
    bw_write_bb(bw, c->entmode);
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_bit(bw, 0UL); /* nolinks */
    bw_write_bs(bw, c->color);
    bw_write_bd(bw, c->ltscale);
    bw_write_bb(bw, c->ltflags);
    bw_write_bb(bw, c->plotflags);
    bw_write_bs(bw, c->invisibility);
    bw_write_rc(bw, c->lineweight);
}

static void write_common_entity_handles(DWG_BW *bw, unsigned long layer_handle,
                                        unsigned long prev_handle, unsigned long next_handle)
{
    bw_write_handle(bw, 3, 0UL); /* xdicobjhandle */
    bw_write_handle(bw, 4, prev_handle);
    bw_write_handle(bw, 4, next_handle);
    bw_write_handle(bw, 5, layer_handle);
}

static void write_owned_entity_handles(DWG_BW *bw, unsigned long owner_handle,
                                       unsigned long layer_handle,
                                       unsigned long prev_handle, unsigned long next_handle)
{
    bw_write_handle(bw, 4, owner_handle);
    bw_write_handle(bw, 3, 0UL);
    bw_write_handle(bw, 4, prev_handle);
    bw_write_handle(bw, 4, next_handle);
    bw_write_handle(bw, 5, layer_handle);
}

/* ================= per-entity-type contexts + field/handle writers ================= */

typedef struct { double sx, sy, sz, ex, ey, ez; unsigned long layer, prev, next; DWG_R2000_COMMON_W c; } LINE_CTX;

static void line_fields(DWG_BW *bw, const void *vctx)
{
    const LINE_CTX *ctx = (const LINE_CTX *)vctx;
    unsigned long z_zero = (ctx->sz == 0.0 && ctx->ez == 0.0) ? 1UL : 0UL;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_bit(bw, z_zero);
    bw_write_rd(bw, ctx->sx);
    bw_write_dd(bw, ctx->sx, ctx->ex);
    bw_write_rd(bw, ctx->sy);
    bw_write_dd(bw, ctx->sy, ctx->ey);
    if (!z_zero)
    {
        bw_write_rd(bw, ctx->sz);
        bw_write_dd(bw, ctx->sz, ctx->ez);
    }
    bw_write_bt(bw, 0.0);
    bw_write_be(bw, 0.0, 0.0, 1.0);
}

static void line_handles(DWG_BW *bw, const void *vctx)
{
    const LINE_CTX *ctx = (const LINE_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double cx, cy, cz, radius; unsigned long layer, prev, next; DWG_R2000_COMMON_W c; } CIRCLE_CTX;

static void circle_fields(DWG_BW *bw, const void *vctx)
{
    const CIRCLE_CTX *ctx = (const CIRCLE_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_3bd(bw, ctx->cx, ctx->cy, ctx->cz);
    bw_write_bd(bw, ctx->radius);
    bw_write_bt(bw, 0.0);
    bw_write_be(bw, 0.0, 0.0, 1.0);
}

static void circle_handles(DWG_BW *bw, const void *vctx)
{
    const CIRCLE_CTX *ctx = (const CIRCLE_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double cx, cy, cz, radius, start_angle, end_angle; unsigned long layer, prev, next; DWG_R2000_COMMON_W c; } ARC_CTX;

static void arc_fields(DWG_BW *bw, const void *vctx)
{
    const ARC_CTX *ctx = (const ARC_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_3bd(bw, ctx->cx, ctx->cy, ctx->cz);
    bw_write_bd(bw, ctx->radius);
    bw_write_bt(bw, 0.0);
    bw_write_be(bw, 0.0, 0.0, 1.0);
    bw_write_bd(bw, ctx->start_angle);
    bw_write_bd(bw, ctx->end_angle);
}

static void arc_handles(DWG_BW *bw, const void *vctx)
{
    const ARC_CTX *ctx = (const ARC_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double px, py, pz; unsigned long layer, prev, next; DWG_R2000_COMMON_W c; } POINT_CTX;

static void point_fields(DWG_BW *bw, const void *vctx)
{
    const POINT_CTX *ctx = (const POINT_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_3bd(bw, ctx->px, ctx->py, ctx->pz);
    bw_write_bt(bw, 0.0);
    bw_write_be(bw, 0.0, 0.0, 1.0);
    bw_write_bd(bw, 0.0);
}

static void point_handles(DWG_BW *bw, const void *vctx)
{
    const POINT_CTX *ctx = (const POINT_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double ix, iy, iz, ax, ay, height, rotation_deg, oblique_deg, width_factor;
                unsigned long horiz_align; const char *text; unsigned long style, layer, prev, next;
                DWG_R2000_COMMON_W c; } TEXT_CTX;

static void text_fields(DWG_BW *bw, const void *vctx)
{
    const TEXT_CTX *ctx = (const TEXT_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_rc(bw, 0U); /* DataFlags = 0: every optional field present */
    bw_write_rd(bw, ctx->iz);
    bw_write_rd(bw, ctx->ix);
    bw_write_rd(bw, ctx->iy);
    bw_write_dd(bw, ctx->ix, ctx->ax);
    bw_write_dd(bw, ctx->iy, ctx->ay);
    bw_write_be(bw, 0.0, 0.0, 1.0);
    bw_write_bt(bw, 0.0);
    bw_write_rd(bw, ctx->oblique_deg * M_PI / 180.0);
    bw_write_rd(bw, ctx->rotation_deg * M_PI / 180.0);
    bw_write_rd(bw, ctx->height);
    bw_write_rd(bw, ctx->width_factor);
    bw_write_t(bw, ctx->text);
    bw_write_bs(bw, 0UL);           /* Generation (backward/upside-down mirror flags):
                                        not modeled by dwg_text's read side either
                                        (see decode_text's own comment), so hardcoding
                                        this is symmetric with the reader, not a gap
                                        introduced here. */
    bw_write_bs(bw, ctx->horiz_align);
    bw_write_bs(bw, 0UL);           /* Vert align: not modeled by dwg_text at all (no getter/setter exists) */
}

static void text_handles(DWG_BW *bw, const void *vctx)
{
    const TEXT_CTX *ctx = (const TEXT_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
    bw_write_handle(bw, 5, ctx->style);
}

typedef struct { double ix, iy, iz, rect_width, text_height, line_space; unsigned long attach;
                const char *text; unsigned long style, layer, prev, next; DWG_R2000_COMMON_W c; } MTEXT_CTX;

static void mtext_fields(DWG_BW *bw, const void *vctx)
{
    const MTEXT_CTX *ctx = (const MTEXT_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_3bd(bw, ctx->ix, ctx->iy, ctx->iz);
    bw_write_3bd(bw, 0.0, 0.0, 1.0);
    bw_write_3bd(bw, 1.0, 0.0, 0.0);
    bw_write_bd(bw, ctx->rect_width);
    bw_write_bd(bw, ctx->text_height);
    bw_write_bs(bw, ctx->attach);
    bw_write_bs(bw, 1UL);   /* drawing direction: no getter/setter exists on dwg_mtext at all */
    bw_write_bd(bw, 0.0);   /* extents height: undocumented field, not modeled */
    bw_write_bd(bw, 0.0);   /* extents width: undocumented field, not modeled */
    bw_write_t(bw, ctx->text);
    bw_write_bs(bw, 1UL);   /* linespacing style: no getter/setter exists on dwg_mtext at all */
    bw_write_bd(bw, ctx->line_space);
    bw_write_bit(bw, 0UL);  /* unknown bit: undocumented field, not modeled */
}

static void mtext_handles(DWG_BW *bw, const void *vctx)
{
    const MTEXT_CTX *ctx = (const MTEXT_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
    bw_write_handle(bw, 5, ctx->style);
}

typedef struct { double x1,y1,z1,x2,y2,z2,x3,y3,z3,x4,y4,z4; unsigned long layer, prev, next; DWG_R2000_COMMON_W c; } SOLID_CTX;

static void solid_fields(DWG_BW *bw, const void *vctx)
{
    const SOLID_CTX *ctx = (const SOLID_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_bt(bw, 0.0);
    bw_write_bd(bw, ctx->z1);
    bw_write_rd(bw, ctx->x1); bw_write_rd(bw, ctx->y1);
    bw_write_rd(bw, ctx->x2); bw_write_rd(bw, ctx->y2);
    bw_write_rd(bw, ctx->x3); bw_write_rd(bw, ctx->y3);
    bw_write_rd(bw, ctx->x4); bw_write_rd(bw, ctx->y4);
    bw_write_be(bw, 0.0, 0.0, 1.0);
}

static void solid_handles(DWG_BW *bw, const void *vctx)
{
    const SOLID_CTX *ctx = (const SOLID_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double ix, iy, iz, sx, sy, sz, rotation_deg; unsigned long block_header, layer, prev, next; DWG_R2000_COMMON_W c; } INSERT_CTX;

static void insert_fields(DWG_BW *bw, const void *vctx)
{
    const INSERT_CTX *ctx = (const INSERT_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_3bd(bw, ctx->ix, ctx->iy, ctx->iz);
    bw_write_bb(bw, 0UL);
    bw_write_rd(bw, ctx->sx);
    bw_write_dd(bw, ctx->sx, ctx->sy);
    bw_write_dd(bw, ctx->sx, ctx->sz);
    bw_write_bd(bw, ctx->rotation_deg * M_PI / 180.0);
    bw_write_3bd(bw, 0.0, 0.0, 1.0);
    bw_write_bit(bw, 0UL); /* has ATTRIBs */
}

static void insert_handles(DWG_BW *bw, const void *vctx)
{
    const INSERT_CTX *ctx = (const INSERT_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
    bw_write_handle(bw, 5, ctx->block_header);
}

typedef struct { double x, y, bulge, start_width, end_width; unsigned long owner, layer, prev, next; DWG_R2000_COMMON_W c; } VERTEX2D_CTX;

static void vertex2d_fields(DWG_BW *bw, const void *vctx)
{
    const VERTEX2D_CTX *ctx = (const VERTEX2D_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_rc(bw, 0U);
    bw_write_3bd(bw, ctx->x, ctx->y, 0.0);
    if (ctx->start_width == ctx->end_width)
        bw_write_bd(bw, ctx->start_width != 0.0 ? -ctx->start_width : 0.0);
    else
    {
        bw_write_bd(bw, ctx->start_width);
        bw_write_bd(bw, ctx->end_width);
    }
    bw_write_bd(bw, ctx->bulge);
    bw_write_bd(bw, 0.0);
}

static void vertex2d_handles(DWG_BW *bw, const void *vctx)
{
    const VERTEX2D_CTX *ctx = (const VERTEX2D_CTX *)vctx;
    write_owned_entity_handles(bw, ctx->owner, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { unsigned long owner, layer, prev, next; DWG_R2000_COMMON_W c; } SEQEND_CTX;

static void seqend_fields(DWG_BW *bw, const void *vctx)
{
    const SEQEND_CTX *ctx = (const SEQEND_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
}

static void seqend_handles(DWG_BW *bw, const void *vctx)
{
    const SEQEND_CTX *ctx = (const SEQEND_CTX *)vctx;
    write_owned_entity_handles(bw, ctx->owner, ctx->layer, ctx->prev, ctx->next);
}

typedef struct { double elevation; long closed; unsigned long layer, prev, next; unsigned long first_vertex, last_vertex, seqend; DWG_R2000_COMMON_W c; } POLYLINE2D_CTX;

static void polyline2d_fields(DWG_BW *bw, const void *vctx)
{
    const POLYLINE2D_CTX *ctx = (const POLYLINE2D_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_bs(bw, ctx->closed ? 0x0001UL : 0x0000UL);
    bw_write_bs(bw, 0UL);
    bw_write_bd(bw, 0.0);
    bw_write_bd(bw, 0.0);
    bw_write_bt(bw, 0.0);
    bw_write_bd(bw, ctx->elevation);
    bw_write_be(bw, 0.0, 0.0, 1.0);
}

static void polyline2d_handles(DWG_BW *bw, const void *vctx)
{
    const POLYLINE2D_CTX *ctx = (const POLYLINE2D_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
    bw_write_handle(bw, 4, ctx->first_vertex);
    bw_write_handle(bw, 4, ctx->last_vertex);
    bw_write_handle(bw, 3, ctx->seqend);
}

/* VERTEX(3D) (type 0x0B): real per-vertex x/y/z, no bulge/width/tangent
   concept at all (unlike VERTEX2D) -- see decode_vertex3d_and_advance's
   own comment in dwg_r2000_entity_reader.c. Handle-stream shape assumed
   identical to VERTEX2D's (owner/xdic/prev/next/LAYER, all "owned by
   the POLYLINE" via entmode==0) by direct structural analogy -- not
   independently re-derived bit-for-bit for this type specifically,
   unlike everything else in this file, which was confirmed against a
   real sample before being trusted. */
typedef struct { double x, y, z; unsigned long owner, layer, prev, next; DWG_R2000_COMMON_W c; } VERTEX3D_CTX;

static void vertex3d_fields(DWG_BW *bw, const void *vctx)
{
    const VERTEX3D_CTX *ctx = (const VERTEX3D_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_rc(bw, 0U);
    bw_write_3bd(bw, ctx->x, ctx->y, ctx->z);
}

static void vertex3d_handles(DWG_BW *bw, const void *vctx)
{
    const VERTEX3D_CTX *ctx = (const VERTEX3D_CTX *)vctx;
    write_owned_entity_handles(bw, ctx->owner, ctx->layer, ctx->prev, ctx->next);
}

/* POLYLINE(3D) (type 0x10): two RC flag bytes (first splined-related,
   not modeled; second's bit 0 = closed) instead of 2D's BS Flags +
   Curve type + widths + thickness + elevation. No Elevation field at
   all -- each VERTEX3D carries its own real Z. Handle-stream shape
   confirmed identical to POLYLINE2D's (LAYER + first/last-vertex +
   SEQEND) by decode_polyline3d itself, which shares the exact same
   handle-reading code path as decode_polyline2d. */
typedef struct { long closed; unsigned long layer, prev, next; unsigned long first_vertex, last_vertex, seqend; DWG_R2000_COMMON_W c; } POLYLINE3D_CTX;

static void polyline3d_fields(DWG_BW *bw, const void *vctx)
{
    const POLYLINE3D_CTX *ctx = (const POLYLINE3D_CTX *)vctx;
    write_common_entity_fields(bw, &ctx->c);
    bw_write_rc(bw, 0U); /* flags 1: splined-related, not modeled */
    bw_write_rc(bw, ctx->closed ? 0x01U : 0x00U); /* flags 2: bit 0 = closed */
}

static void polyline3d_handles(DWG_BW *bw, const void *vctx)
{
    const POLYLINE3D_CTX *ctx = (const POLYLINE3D_CTX *)vctx;
    write_common_entity_handles(bw, ctx->layer, ctx->prev, ctx->next);
    bw_write_handle(bw, 4, ctx->first_vertex);
    bw_write_handle(bw, 4, ctx->last_vertex);
    bw_write_handle(bw, 3, ctx->seqend);
}

typedef struct
{
    const char *name;
    const unsigned char *flags_bits; /* 29 raw bits, copied verbatim from the template */
    unsigned long xdic, owner, reserved, block_entity, first_entity, last_entity, endblk, layout;
} BLOCK_HEADER_CTX;

static void block_header_fields(DWG_BW *bw, const void *vctx)
{
    const BLOCK_HEADER_CTX *ctx = (const BLOCK_HEADER_CTX *)vctx;
    unsigned long i;
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_entry_name(bw, ctx->name);
    for (i = 0UL; i < 29UL; i++)
        bw_write_bit(bw, ctx->flags_bits[i]);
}

static void block_header_handles(DWG_BW *bw, const void *vctx)
{
    const BLOCK_HEADER_CTX *ctx = (const BLOCK_HEADER_CTX *)vctx;
    bw_write_handle(bw, 4, ctx->owner);
    bw_write_handle(bw, 3, ctx->xdic);
    bw_write_handle(bw, 5, ctx->reserved);
    bw_write_handle(bw, 3, ctx->block_entity);
    bw_write_handle(bw, 4, ctx->first_entity);
    bw_write_handle(bw, 4, ctx->last_entity);
    bw_write_handle(bw, 3, ctx->endblk);
    bw_write_handle(bw, 5, ctx->layout);
}

#define DWG_R2000_TYPE_LAYER_CONTROL 0x32

typedef struct
{
    const char *name;
    const unsigned char *flags_bits; /* 32 raw bits, copied verbatim from a template LAYER */
    unsigned long owner;             /* LAYER_CONTROL's handle */
    unsigned long reserved_null;     /* always 0 in every real LAYER checked */
    unsigned long placeholder;       /* shared class-defined object every real LAYER referenced (plot style slot) */
    unsigned long ltype;             /* resolved LTYPE handle */
} LAYER_CTX;

static void layer_fields(DWG_BW *bw, const void *vctx)
{
    const LAYER_CTX *ctx = (const LAYER_CTX *)vctx;
    unsigned long i;
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_entry_name(bw, ctx->name);
    for (i = 0UL; i < 32UL; i++)
        bw_write_bit(bw, ctx->flags_bits[i]);
}

static void layer_handles(DWG_BW *bw, const void *vctx)
{
    const LAYER_CTX *ctx = (const LAYER_CTX *)vctx;
    bw_write_handle(bw, 4, ctx->owner);
    bw_write_handle(bw, 3, 0UL); /* xdicobjhandle -- a fresh layer doesn't need its own extension dictionary */
    bw_write_handle(bw, 5, ctx->reserved_null);
    bw_write_handle(bw, 5, ctx->placeholder);
    bw_write_handle(bw, 5, ctx->ltype);
}

typedef struct
{
    unsigned long owner, xdic;
    unsigned long count;
    const unsigned long *entries;
} LAYER_CONTROL_CTX;

static void layer_control_fields(DWG_BW *bw, const void *vctx)
{
    const LAYER_CONTROL_CTX *ctx = (const LAYER_CONTROL_CTX *)vctx;
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_bs(bw, ctx->count);
}

static void layer_control_handles(DWG_BW *bw, const void *vctx)
{
    const LAYER_CONTROL_CTX *ctx = (const LAYER_CONTROL_CTX *)vctx;
    unsigned long i;
    bw_write_handle(bw, 4, ctx->owner);
    bw_write_handle(bw, 3, ctx->xdic);
    for (i = 0UL; i < ctx->count; i++)
        bw_write_handle(bw, 2, ctx->entries[i]);
}

#define DWG_R2000_TYPE_STYLE_CONTROL 0x34
#define DWG_R2000_MAX_STYLE_FLAGS_BITS 1024UL

typedef struct
{
    const char *name;
    const unsigned char *flags_bits; /* variable length (embeds the font filename) -- copied verbatim from a template STYLE */
    unsigned long flags_bit_count;
    unsigned long owner;             /* STYLE_CONTROL's handle */
    unsigned long extra;             /* the one post-xdic handle every real STYLE checked had as NULL */
} STYLE_CTX;

static void style_fields(DWG_BW *bw, const void *vctx)
{
    const STYLE_CTX *ctx = (const STYLE_CTX *)vctx;
    unsigned long i;
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_entry_name(bw, ctx->name);
    for (i = 0UL; i < ctx->flags_bit_count; i++)
        bw_write_bit(bw, ctx->flags_bits[i]);
}

static void style_handles(DWG_BW *bw, const void *vctx)
{
    const STYLE_CTX *ctx = (const STYLE_CTX *)vctx;
    bw_write_handle(bw, 4, ctx->owner);
    bw_write_handle(bw, 3, 0UL); /* xdicobjhandle -- a fresh style doesn't need its own extension dictionary */
    bw_write_handle(bw, 5, ctx->extra);
}

typedef struct
{
    unsigned long owner, xdic;
    unsigned long count;
    const unsigned long *entries;
} STYLE_CONTROL_CTX;

static void style_control_fields(DWG_BW *bw, const void *vctx)
{
    const STYLE_CONTROL_CTX *ctx = (const STYLE_CONTROL_CTX *)vctx;
    bw_write_bl(bw, 0UL); /* numreactors */
    bw_write_bs(bw, ctx->count);
}

static void style_control_handles(DWG_BW *bw, const void *vctx)
{
    const STYLE_CONTROL_CTX *ctx = (const STYLE_CONTROL_CTX *)vctx;
    unsigned long i;
    bw_write_handle(bw, 4, ctx->owner);
    bw_write_handle(bw, 3, ctx->xdic);
    for (i = 0UL; i < ctx->count; i++)
        bw_write_handle(bw, 2, ctx->entries[i]);
}

/* ================= template reading helpers (mirrors the reader's own bit-level idioms) ================= */

/* read_whole_file removed -- use dwg_read_whole_file from dwg_file_io.c */

static int objmap_find_w(const DWG_R2000_OBJMAP *objmap, unsigned long handle, unsigned long *loc)
{
    unsigned long i;
    for (i = 0UL; i < objmap->count; i++)
    {
        if (objmap->entries[i].handle == handle)
        {
            *loc = objmap->entries[i].location;
            return 1;
        }
    }
    return 0;
}

/* Skips a chain of EED blocks (|Length(BS)|AppHandle(H)|data[Length]|...|0|),
   mirroring skip_eed_blocks in dwg_r2000_entity_reader.c (kept private
   there, small enough to duplicate rather than export). */
static void skip_eed_blocks_w(DWG_BITSTREAM *bs, unsigned short first_length)
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

/* Decodes a Common-non-entity-object-format record's Entry name (LAYER,
   STYLE, BLOCK_HEADER all share this prefix -- see the reference doc).
   Returns 1 and fills name_buf on success (matching object type), 0 if
   this object isn't of expected_type or has graphics/other unsupported
   content. */
static int decode_table_record_name_w(const unsigned char *data, unsigned long length,
                                      unsigned long loc, unsigned short expected_type,
                                      char *name_buf, unsigned long buf_size)
{
    DWG_BITSTREAM bs;
    unsigned short obj_type, eed_size;
    unsigned char code;
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != expected_type)
        return 0;

    (void)dwg_bs_read_rl(&bs);
    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks_w(&bs, eed_size);

    (void)dwg_bs_read_bl(&bs);
    (void)dwg_bs_read_t(&bs, name_buf, (unsigned short)buf_size);

    return 1;
}

/* Scans every object in the map for one of `type_code` whose decoded
   name matches `name`. Returns 1 and fills *out_handle on success. */
static int find_table_record_handle(const unsigned char *data, unsigned long length,
                                    const DWG_R2000_OBJMAP *objmap, unsigned short type_code,
                                    const char *name, unsigned long *out_handle)
{
    unsigned long i;
    char name_buf[256];

    for (i = 0UL; i < objmap->count; i++)
    {
        if (decode_table_record_name_w(data, length, objmap->entries[i].location,
                                       type_code, name_buf, sizeof(name_buf)))
        {
            if (strcmp(name_buf, name) == 0)
            {
                *out_handle = objmap->entries[i].handle;
                return 1;
            }
        }
    }
    return 0;
}


typedef struct
{
    unsigned long name_len_unused;
    char name[256];
    unsigned char flags_bits[29];
    unsigned long owner, xdic, reserved, block_entity, first_entity, last_entity, endblk, layout;
} BLOCK_HEADER_RAW;

static int get_block_header_raw(const unsigned char *data, unsigned long length,
                                unsigned long loc, BLOCK_HEADER_RAW *out)
{
    DWG_BITSTREAM bs;
    unsigned long after_ms_bit, handles_start_bit, obj_size, i;
    unsigned short obj_type, eed_size;
    unsigned char code;
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    after_ms_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_BLOCK_HEADER)
        return 0;
    obj_size = dwg_bs_read_rl(&bs);
    handles_start_bit = after_ms_bit + obj_size;

    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks_w(&bs, eed_size);

    (void)dwg_bs_read_bl(&bs);
    (void)dwg_bs_read_t(&bs, out->name, (unsigned short)sizeof(out->name));

    for (i = 0UL; i < 29UL; i++)
        out->flags_bits[i] = (unsigned char)dwg_bs_read_bit(&bs);

    dwg_bs_seek_bit(&bs, handles_start_bit);
    dwg_bs_read_handle(&bs, &code, &value); out->owner = value;
    dwg_bs_read_handle(&bs, &code, &value); out->xdic = value;
    dwg_bs_read_handle(&bs, &code, &value); out->reserved = value;
    dwg_bs_read_handle(&bs, &code, &value); out->block_entity = value;
    dwg_bs_read_handle(&bs, &code, &value); out->first_entity = value;
    dwg_bs_read_handle(&bs, &code, &value); out->last_entity = value;
    dwg_bs_read_handle(&bs, &code, &value); out->endblk = value;
    dwg_bs_read_handle(&bs, &code, &value); out->layout = value;

    return 1;
}

typedef struct
{
    unsigned char flags_bits[32];
    unsigned long owner, reserved_null, placeholder, ltype;
} LAYER_RAW;

/* Reads a template LAYER's fixed 32-bit flags region and its 3 handle-
   stream fields after xdicobjhandle verbatim (reserved/NULL, a shared
   class-defined placeholder object, and its LTYPE) -- see
   reverse/DWG_R2000_format_reference.md's LAYER writer entry for how
   these were derived empirically (no spec table available for this
   part). A new LAYER copies flags_bits/reserved_null/placeholder
   verbatim from this and resolves its own LTYPE separately. */
static int get_layer_raw(const unsigned char *data, unsigned long length,
                         unsigned long loc, LAYER_RAW *out)
{
    DWG_BITSTREAM bs;
    unsigned long after_ms_bit, handles_start_bit, obj_size, i;
    unsigned short obj_type, eed_size;
    unsigned char code, name_buf[256];
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    after_ms_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_LAYER)
        return 0;
    obj_size = dwg_bs_read_rl(&bs);
    handles_start_bit = after_ms_bit + obj_size;

    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks_w(&bs, eed_size);

    (void)dwg_bs_read_bl(&bs);
    (void)dwg_bs_read_t(&bs, (char *)name_buf, (unsigned short)sizeof(name_buf));

    for (i = 0UL; i < 32UL; i++)
        out->flags_bits[i] = (unsigned char)dwg_bs_read_bit(&bs);

    dwg_bs_seek_bit(&bs, handles_start_bit);
    dwg_bs_read_handle(&bs, &code, &value); out->owner = value; /* LAYER_CONTROL, unused by caller */
    dwg_bs_read_handle(&bs, &code, &value); /* xdicobjhandle, not copied */
    dwg_bs_read_handle(&bs, &code, &value); out->reserved_null = value;
    dwg_bs_read_handle(&bs, &code, &value); out->placeholder = value;
    dwg_bs_read_handle(&bs, &code, &value); out->ltype = value;

    return 1;
}

typedef struct
{
    unsigned long owner, xdic, count;
    unsigned long entries[256];
} LAYER_CONTROL_RAW;

static int get_layer_control_raw(const unsigned char *data, unsigned long length,
                                 unsigned long loc, LAYER_CONTROL_RAW *out)
{
    DWG_BITSTREAM bs;
    unsigned long after_ms_bit, handles_start_bit, obj_size, i;
    unsigned short obj_type, eed_size;
    unsigned char code;
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    after_ms_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_LAYER_CONTROL)
        return 0;
    obj_size = dwg_bs_read_rl(&bs);
    handles_start_bit = after_ms_bit + obj_size;

    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        return 0;

    (void)dwg_bs_read_bl(&bs);
    out->count = dwg_bs_read_bs(&bs);
    if (out->count > 255UL)
        return 0;

    dwg_bs_seek_bit(&bs, handles_start_bit);
    dwg_bs_read_handle(&bs, &code, &value); out->owner = value;
    dwg_bs_read_handle(&bs, &code, &value); out->xdic = value;
    for (i = 0UL; i < out->count; i++)
    {
        dwg_bs_read_handle(&bs, &code, &value);
        out->entries[i] = value;
    }

    return 1;
}

typedef struct
{
    unsigned char flags_bits[DWG_R2000_MAX_STYLE_FLAGS_BITS];
    unsigned long flags_bit_count;
    unsigned long extra;
} STYLE_RAW;

/* Same "copy verbatim, don't need full understanding" approach as
   get_layer_raw, except STYLE's flags region is genuinely variable-
   length (it embeds the font filename, e.g. "arial.ttf", as a T-string
   among fixed numeric fields) rather than LAYER's fixed 32 bits -- so
   this stores both the bits and how many there were, from whichever
   template STYLE object was read, and a new STYLE just clones that
   whole region (font/height/width-factor/etc.) verbatim, varying only
   the entry name. See DWG_R2000_format_reference.md. */
static int get_style_raw(const unsigned char *data, unsigned long length,
                         unsigned long loc, STYLE_RAW *out)
{
    DWG_BITSTREAM bs;
    unsigned long after_ms_bit, handles_start_bit, obj_size, i, flags_start;
    unsigned short obj_type, eed_size;
    unsigned char code, name_buf[256];
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    after_ms_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_STYLE)
        return 0;
    obj_size = dwg_bs_read_rl(&bs);
    handles_start_bit = after_ms_bit + obj_size;

    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks_w(&bs, eed_size);

    (void)dwg_bs_read_bl(&bs);
    (void)dwg_bs_read_t(&bs, (char *)name_buf, (unsigned short)sizeof(name_buf));

    flags_start = dwg_bs_tell_bit(&bs);
    out->flags_bit_count = handles_start_bit - flags_start;
    if (out->flags_bit_count > DWG_R2000_MAX_STYLE_FLAGS_BITS)
        return 0;
    for (i = 0UL; i < out->flags_bit_count; i++)
        out->flags_bits[i] = (unsigned char)dwg_bs_read_bit(&bs);

    dwg_bs_seek_bit(&bs, handles_start_bit);
    dwg_bs_read_handle(&bs, &code, &value); /* owner (STYLE_CONTROL), unused by caller */
    dwg_bs_read_handle(&bs, &code, &value); /* xdicobjhandle, not copied */
    dwg_bs_read_handle(&bs, &code, &value); out->extra = value;

    return 1;
}

typedef struct
{
    unsigned long owner, xdic, count;
    unsigned long entries[256];
} STYLE_CONTROL_RAW;

static int get_style_control_raw(const unsigned char *data, unsigned long length,
                                 unsigned long loc, STYLE_CONTROL_RAW *out)
{
    DWG_BITSTREAM bs;
    unsigned long after_ms_bit, handles_start_bit, obj_size, i;
    unsigned short obj_type, eed_size;
    unsigned char code;
    unsigned long value;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    after_ms_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_STYLE_CONTROL)
        return 0;
    obj_size = dwg_bs_read_rl(&bs);
    handles_start_bit = after_ms_bit + obj_size;

    dwg_bs_read_handle(&bs, &code, &value);
    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        return 0;

    (void)dwg_bs_read_bl(&bs);
    out->count = dwg_bs_read_bs(&bs);
    if (out->count > 255UL)
        return 0;

    dwg_bs_seek_bit(&bs, handles_start_bit);
    dwg_bs_read_handle(&bs, &code, &value); out->owner = value;
    dwg_bs_read_handle(&bs, &code, &value); out->xdic = value;
    for (i = 0UL; i < out->count; i++)
    {
        dwg_bs_read_handle(&bs, &code, &value);
        out->entries[i] = value;
    }

    return 1;
}

#define MAX_NEW_OBJECTS 4096UL
#define MAX_NEW_LAYERS 256UL

typedef struct
{
    char name[64];
    unsigned long handle;
} LAYER_REG_ENTRY;

/* Resolves `name` to a LAYER handle: an exact match in the template,
   or one already created earlier in this same write (`new_layers`), or
   -- if neither -- encodes a brand new LAYER object (copying the
   flags/reserved/placeholder/ltype fields verbatim from
   `layer_tmpl`, see get_layer_raw's own comment for why) and appends
   it to both the object list (obj_bytes/obj_lens/obj_handles, same
   arrays used for entities -- object position is arbitrary, only the
   object map's handle->location entries matter) and `new_layers`
   (so a second entity on the same new layer name reuses the same
   handle instead of creating a duplicate). Returns 0 (NULL handle) on
   allocation/capacity failure -- caller must treat that as a hard
   write failure, not silently fall back, since a LINE with handle 0
   for LAYER is not a valid file. */
static unsigned long resolve_or_create_layer(const unsigned char *data, unsigned long length,
                                             const DWG_R2000_OBJMAP *objmap, const char *name,
                                             unsigned long layer_control_handle,
                                             int have_layer_control, int have_layer_template,
                                             const LAYER_RAW *layer_tmpl,
                                             LAYER_REG_ENTRY *new_layers, unsigned long *n_new_layers,
                                             unsigned long *next_handle,
                                             unsigned char **obj_bytes, unsigned long *obj_lens,
                                             unsigned long *obj_handles, unsigned long *n_objs)
{
    unsigned long h, i;
    LAYER_CTX ctx;

    /* An entity's layer defaults to "" (dwg_entity_create's own
       convention, see dwg_entity.c) meaning "the default layer" -- NOT
       a real layer named "" to create. Route that (and, defensively,
       any future caller that explicitly asks for the empty name) to
       "0", this engine's actual default layer, before ever considering
       creating a new one -- matches the fallback resolve_layer_handle
       already used to do for entities without an explicit layer. */
    if (name[0] == '\0')
        name = "0";

    if (find_table_record_handle(data, length, objmap, (unsigned short)DWG_R2000_TYPE_LAYER, name, &h))
        return h;

    for (i = 0UL; i < *n_new_layers; i++)
        if (strcmp(new_layers[i].name, name) == 0)
            return new_layers[i].handle;

    if (!have_layer_control || !have_layer_template)
        return 0UL; /* can't create: no LAYER_CONTROL/template LAYER found in this template */
    if (*n_new_layers >= MAX_NEW_LAYERS || *n_objs >= MAX_NEW_OBJECTS)
        return 0UL;

    h = (*next_handle)++;
    ctx.name = name;
    ctx.flags_bits = layer_tmpl->flags_bits;
    ctx.owner = layer_control_handle;
    ctx.reserved_null = layer_tmpl->reserved_null;
    ctx.placeholder = layer_tmpl->placeholder;
    ctx.ltype = layer_tmpl->ltype;

    obj_bytes[*n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_LAYER, h,
                                             layer_fields, layer_handles, &ctx, &obj_lens[*n_objs]);
    if (obj_bytes[*n_objs] == NULL)
        return 0UL;
    obj_handles[*n_objs] = h;
    (*n_objs)++;

    strncpy(new_layers[*n_new_layers].name, name, sizeof(new_layers[0].name) - 1UL);
    new_layers[*n_new_layers].name[sizeof(new_layers[0].name) - 1UL] = '\0';
    new_layers[*n_new_layers].handle = h;
    (*n_new_layers)++;

    return h;
}

/* Same idea as resolve_or_create_layer, for STYLE: a TEXT/MTEXT's
   style_name defaults to "" (dwg_add_text/dwg_add_mtext's own
   convention) meaning "the default style", routed to "Standard" --
   this engine's actual default -- before ever considering creating a
   new one, same fix as the layer resolver needed. A new STYLE clones
   the template's whole flags region (font/height/width-factor/etc.)
   verbatim, varying only the entry name -- see get_style_raw. */
static unsigned long resolve_or_create_style(const unsigned char *data, unsigned long length,
                                             const DWG_R2000_OBJMAP *objmap, const char *name,
                                             unsigned long style_control_handle,
                                             int have_style_control, int have_style_template,
                                             const STYLE_RAW *style_tmpl,
                                             LAYER_REG_ENTRY *new_styles, unsigned long *n_new_styles,
                                             unsigned long *next_handle,
                                             unsigned char **obj_bytes, unsigned long *obj_lens,
                                             unsigned long *obj_handles, unsigned long *n_objs)
{
    unsigned long h, i;
    STYLE_CTX ctx;

    if (name[0] == '\0')
        name = "Standard";

    if (find_table_record_handle(data, length, objmap, (unsigned short)DWG_R2000_TYPE_STYLE, name, &h))
        return h;

    for (i = 0UL; i < *n_new_styles; i++)
        if (strcmp(new_styles[i].name, name) == 0)
            return new_styles[i].handle;

    if (!have_style_control || !have_style_template)
        return 0UL;
    if (*n_new_styles >= MAX_NEW_LAYERS || *n_objs >= MAX_NEW_OBJECTS)
        return 0UL;

    h = (*next_handle)++;
    ctx.name = name;
    ctx.flags_bits = style_tmpl->flags_bits;
    ctx.flags_bit_count = style_tmpl->flags_bit_count;
    ctx.owner = style_control_handle;
    ctx.extra = style_tmpl->extra;

    obj_bytes[*n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_STYLE, h,
                                             style_fields, style_handles, &ctx, &obj_lens[*n_objs]);
    if (obj_bytes[*n_objs] == NULL)
        return 0UL;
    obj_handles[*n_objs] = h;
    (*n_objs)++;

    strncpy(new_styles[*n_new_styles].name, name, sizeof(new_styles[0].name) - 1UL);
    new_styles[*n_new_styles].name[sizeof(new_styles[0].name) - 1UL] = '\0';
    new_styles[*n_new_styles].handle = h;
    (*n_new_styles)++;

    return h;
}

static void get_object_span(const unsigned char *data, unsigned long length,
                            unsigned long loc, unsigned long *start, unsigned long *end)
{
    DWG_BITSTREAM bs;
    unsigned long ms_len, after_ms_byte;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    ms_len = dwg_bs_read_ms(&bs);
    after_ms_byte = dwg_bs_tell_bit(&bs) / 8UL;

    *start = loc;
    *end = after_ms_byte + ms_len + 2UL;
}

/* ================= object-map / header patching ================= */

typedef struct
{
    unsigned long handle;
    unsigned long location;
} HL_PAIR;

static int hl_pair_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const HL_PAIR *)a)->handle;
    unsigned long hb = ((const HL_PAIR *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

/* ================= splice: N non-overlapping "replace this existing
   object's bytes" regions, applied in file-position order =================
 *
 * Two regions are always present (BLOCK_HEADER, the Model_Space entity
 * span); a third (LAYER_CONTROL) is added only when the write actually
 * creates a new LAYER. Brand new content (new entities, new LAYER
 * objects) isn't its own region -- object positions are arbitrary,
 * linked only by handles, so new objects just get appended into
 * whichever existing region's replacement bytes is convenient (here,
 * always the entities region).
 */

#define MAX_REGIONS 4UL

typedef struct
{
    unsigned long start, end;
    const unsigned char *bytes;
    unsigned long len;
} REGION;

static int region_cmp(const void *a, const void *b)
{
    unsigned long sa = ((const REGION *)a)->start;
    unsigned long sb = ((const REGION *)b)->start;
    if (sa < sb) return -1;
    if (sa > sb) return 1;
    return 0;
}

/* Cumulative byte shift applying to everything at or after `old_offset`
   that lies outside every region (i.e. only fully-preceding regions'
   size deltas count) -- same idea as the Python prototype's shifted()
   helper. boundaries[i]/shifts[i] must be parallel arrays: boundaries[i]
   is region i's *end*, shifts[i] is the cumulative shift once region i
   has been applied (both already sorted by region start === region end
   here since regions never overlap). */
static long region_shift_at(unsigned long old_offset, const unsigned long *boundaries,
                            const long *shifts, unsigned long n)
{
    long s = 0L;
    unsigned long i;
    for (i = 0UL; i < n; i++)
    {
        if (old_offset >= boundaries[i])
            s = shifts[i];
        else
            break;
    }
    return s;
}

/* ================= top-level writer ================= */

DWG_IO_RESULT dwg_write_dwg_r2000(HDWG hDwg, const char *template_path, const char *out_path)
{
    unsigned char *data = NULL;
    unsigned long length = 0UL;
    DWG_R2000_HEADER hdr;
    const DWG_R2000_SECTION_RECORD *objmap_rec;
    DWG_R2000_OBJMAP objmap;
    unsigned long model_space_handle = 0UL, model_space_loc = 0UL;
    BLOCK_HEADER_RAW bh;
    unsigned long old_bh_start, old_bh_end;
    unsigned long old_entities_start, old_entities_end;
    unsigned long max_handle, next_handle;
    unsigned char *new_bh_bytes = NULL; unsigned long new_bh_len = 0UL;
    unsigned char **obj_bytes = NULL; unsigned long *obj_lens = NULL; unsigned long *obj_handles = NULL;
    unsigned long n_objs = 0UL;
    HENTITY e;
    unsigned long *top_handles = NULL; unsigned long n_top = 0UL;
    unsigned long i;
    unsigned char *out_buf = NULL; unsigned long out_len = 0UL;
    HL_PAIR *pairs = NULL; unsigned long n_pairs = 0UL;
    FILE *outfp;
    DWG_IO_RESULT result = DWG_IO_ERROR_FORMAT;
    unsigned long layer_control_handle = 0UL, layer_control_loc = 0UL;
    int have_layer_control = 0, have_layer_template = 0;
    LAYER_CONTROL_RAW lc_raw;
    LAYER_RAW layer_tmpl;
    LAYER_REG_ENTRY *new_layers = NULL; unsigned long n_new_layers = 0UL;
    unsigned char *new_lc_bytes = NULL; unsigned long new_lc_len = 0UL;
    unsigned long old_lc_start = 0UL, old_lc_end = 0UL;
    unsigned long style_control_handle = 0UL, style_control_loc = 0UL;
    int have_style_control = 0, have_style_template = 0;
    STYLE_CONTROL_RAW sc_raw;
    STYLE_RAW style_tmpl;
    LAYER_REG_ENTRY *new_styles = NULL; unsigned long n_new_styles = 0UL;
    unsigned char *new_sc_bytes = NULL; unsigned long new_sc_len = 0UL;
    unsigned long old_sc_start = 0UL, old_sc_end = 0UL;
    REGION regions[MAX_REGIONS]; unsigned long n_regions = 0UL;
    unsigned long region_boundaries[MAX_REGIONS]; long region_shifts[MAX_REGIONS];

    data = dwg_read_whole_file(template_path, &length);
    if (data == NULL)
        return DWG_IO_ERROR_OPEN;

    if (!dwg_r2000_parse_header(data, length, &hdr))
    {
        free(data);
        return DWG_IO_ERROR_FORMAT;
    }

    objmap_rec = dwg_r2000_find_record(&hdr, 2);
    if (objmap_rec == NULL)
    {
        free(data);
        return DWG_IO_ERROR_FORMAT;
    }

    if (!dwg_r2000_parse_object_map(data, length, objmap_rec->seeker, objmap_rec->size, &objmap))
    {
        free(data);
        return DWG_IO_ERROR_FORMAT;
    }

    /* find *Model_Space */
    for (i = 0UL; i < objmap.count; i++)
    {
        char name_buf[256];
        if (decode_table_record_name_w(data, length, objmap.entries[i].location,
                                       DWG_R2000_TYPE_BLOCK_HEADER, name_buf, sizeof(name_buf)))
        {
            if (strcmp(name_buf, "*Model_Space") == 0)
            {
                model_space_handle = objmap.entries[i].handle;
                model_space_loc = objmap.entries[i].location;
                break;
            }
        }
    }
    if (model_space_handle == 0UL)
    {
        dwg_r2000_objmap_free(&objmap);
        free(data);
        return DWG_IO_ERROR_FORMAT;
    }

    /* find LAYER_CONTROL and STYLE_CONTROL (needed only if the write
       ends up creating a new LAYER/STYLE; harmless to look up
       unconditionally, it's cheap) */
    for (i = 0UL; i < objmap.count && (!have_layer_control || !have_style_control); i++)
    {
        DWG_BITSTREAM peek;
        unsigned short peek_type;
        dwg_bs_init(&peek, data, length);
        dwg_bs_seek_bit(&peek, objmap.entries[i].location * 8UL);
        (void)dwg_bs_read_ms(&peek);
        peek_type = dwg_bs_read_bs(&peek);
        if (peek_type == (unsigned short)DWG_R2000_TYPE_LAYER_CONTROL && !have_layer_control)
        {
            layer_control_handle = objmap.entries[i].handle;
            layer_control_loc = objmap.entries[i].location;
            have_layer_control = 1;
        }
        else if (peek_type == (unsigned short)DWG_R2000_TYPE_STYLE_CONTROL && !have_style_control)
        {
            style_control_handle = objmap.entries[i].handle;
            style_control_loc = objmap.entries[i].location;
            have_style_control = 1;
        }
    }
    if (have_layer_control && get_layer_control_raw(data, length, layer_control_loc, &lc_raw) &&
        lc_raw.count > 0UL)
    {
        unsigned long tmpl_loc;
        if (objmap_find_w(&objmap, lc_raw.entries[0], &tmpl_loc) &&
            get_layer_raw(data, length, tmpl_loc, &layer_tmpl))
        {
            have_layer_template = 1;
        }
    }
    if (have_style_control && get_style_control_raw(data, length, style_control_loc, &sc_raw) &&
        sc_raw.count > 0UL)
    {
        unsigned long tmpl_loc;
        if (objmap_find_w(&objmap, sc_raw.entries[0], &tmpl_loc) &&
            get_style_raw(data, length, tmpl_loc, &style_tmpl))
        {
            have_style_template = 1;
        }
    }

    if (!get_block_header_raw(data, length, model_space_loc, &bh))
    {
        dwg_r2000_objmap_free(&objmap);
        free(data);
        return DWG_IO_ERROR_FORMAT;
    }
    get_object_span(data, length, model_space_loc, &old_bh_start, &old_bh_end);

    /* span of Model_Space's current entities (may be empty) */
    if (bh.first_entity != 0UL && bh.last_entity != 0UL)
    {
        unsigned long first_loc, last_loc, dummy;
        if (!objmap_find_w(&objmap, bh.first_entity, &first_loc) ||
            !objmap_find_w(&objmap, bh.last_entity, &last_loc))
        {
            dwg_r2000_objmap_free(&objmap);
            free(data);
            return DWG_IO_ERROR_FORMAT;
        }
        old_entities_start = first_loc;
        get_object_span(data, length, last_loc, &dummy, &old_entities_end);
    }
    else
    {
        /* no entities yet: insert right before ENDBLK */
        unsigned long endblk_loc;
        if (!objmap_find_w(&objmap, bh.endblk, &endblk_loc))
        {
            dwg_r2000_objmap_free(&objmap);
            free(data);
            return DWG_IO_ERROR_FORMAT;
        }
        old_entities_start = endblk_loc;
        old_entities_end = endblk_loc;
    }

    /* pick handles for every new object, comfortably above anything
       the template already uses */
    max_handle = model_space_handle;
    for (i = 0UL; i < objmap.count; i++)
        if (objmap.entries[i].handle > max_handle)
            max_handle = objmap.entries[i].handle;
    next_handle = max_handle + 1000UL;

    obj_bytes = (unsigned char **)calloc(MAX_NEW_OBJECTS, sizeof(unsigned char *));
    obj_lens = (unsigned long *)calloc(MAX_NEW_OBJECTS, sizeof(unsigned long));
    obj_handles = (unsigned long *)calloc(MAX_NEW_OBJECTS, sizeof(unsigned long));
    top_handles = (unsigned long *)calloc(MAX_NEW_OBJECTS, sizeof(unsigned long));
    new_layers = (LAYER_REG_ENTRY *)calloc(MAX_NEW_LAYERS, sizeof(LAYER_REG_ENTRY));
    new_styles = (LAYER_REG_ENTRY *)calloc(MAX_NEW_LAYERS, sizeof(LAYER_REG_ENTRY));
    if (obj_bytes == NULL || obj_lens == NULL || obj_handles == NULL || top_handles == NULL ||
        new_layers == NULL || new_styles == NULL)
    {
        result = DWG_IO_ERROR_MEMORY;
        goto cleanup;
    }

    /* first pass: assign one top-level handle per entity in hDwg's list */
    for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        if (n_top >= MAX_NEW_OBJECTS)
            goto cleanup;
        top_handles[n_top++] = next_handle++;
    }

    /* second pass: encode each entity (and, for POLYLINE, its VERTEX/SEQEND
       sub-objects, appended after the top-level run) */
    {
        unsigned long top_i = 0UL;
        for (e = dwg_document_first_entity(hDwg); e != NULL; e = dwg_document_next_entity(e))
        {
            unsigned long h = top_handles[top_i];
            unsigned long prev = (top_i > 0UL) ? top_handles[top_i - 1UL] : 0UL;
            unsigned long next = (top_i + 1UL < n_top) ? top_handles[top_i + 1UL] : 0UL;
            DWG_ENTITY_TYPE t = dwg_entity_get_type(e);
            unsigned long layer_h = resolve_or_create_layer(data, length, &objmap, dwg_entity_get_layer(e),
                                                            layer_control_handle, have_layer_control,
                                                            have_layer_template, &layer_tmpl,
                                                            new_layers, &n_new_layers, &next_handle,
                                                            obj_bytes, obj_lens, obj_handles, &n_objs);

            top_i++;

            if (layer_h == 0UL)
            {
                result = DWG_IO_ERROR_FORMAT;
                goto cleanup;
            }

            if (t == DWG_ENTITY_LINE)
            {
                LINE_CTX ctx;
                DWG_LINE3D *g = (DWG_LINE3D *)e->geometry;
                common_w_defaults(&ctx.c);
                ctx.sx = g->start.x; ctx.sy = g->start.y; ctx.sz = g->start.z;
                ctx.ex = g->end.x; ctx.ey = g->end.y; ctx.ez = g->end.z;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_LINE, h,
                                                         line_fields, line_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_CIRCLE)
            {
                CIRCLE_CTX ctx;
                DWG_CIRCLE3D *g = (DWG_CIRCLE3D *)e->geometry;
                common_w_defaults(&ctx.c);
                ctx.cx = g->center.x; ctx.cy = g->center.y; ctx.cz = g->center.z;
                ctx.radius = g->radius;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_CIRCLE, h,
                                                         circle_fields, circle_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_ARC)
            {
                ARC_CTX ctx;
                DWG_ARC3D *g = (DWG_ARC3D *)e->geometry;
                common_w_defaults(&ctx.c);
                ctx.cx = g->center.x; ctx.cy = g->center.y; ctx.cz = g->center.z;
                ctx.radius = g->radius;
                ctx.start_angle = g->start_angle * M_PI / 180.0;
                ctx.end_angle = g->end_angle * M_PI / 180.0;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_ARC, h,
                                                         arc_fields, arc_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_POINT)
            {
                POINT_CTX ctx;
                DWG_POINT3D *g = (DWG_POINT3D *)e->geometry;
                common_w_defaults(&ctx.c);
                ctx.px = g->x; ctx.py = g->y; ctx.pz = g->z;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_POINT, h,
                                                         point_fields, point_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_TEXT)
            {
                TEXT_CTX ctx;
                double x, y, z, ax, ay, az;
                common_w_defaults(&ctx.c);
                dwg_text_get_point(e, &x, &y, &z);
                dwg_text_get_point0(e, &ax, &ay, &az);
                ctx.ix = x; ctx.iy = y; ctx.iz = z;
                ctx.ax = ax; ctx.ay = ay;
                ctx.height = dwg_text_get_height(e);
                ctx.rotation_deg = dwg_text_get_angle(e);
                ctx.oblique_deg = dwg_text_get_oblique(e);
                ctx.width_factor = dwg_text_get_width_factor(e);
                ctx.horiz_align = dwg_text_get_align(e);
                ctx.text = dwg_text_get_text(e);
                ctx.style = resolve_or_create_style(data, length, &objmap, dwg_text_get_style_name(e),
                                                    style_control_handle, have_style_control,
                                                    have_style_template, &style_tmpl,
                                                    new_styles, &n_new_styles, &next_handle,
                                                    obj_bytes, obj_lens, obj_handles, &n_objs);
                if (ctx.style == 0UL) { result = DWG_IO_ERROR_FORMAT; goto cleanup; }
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_TEXT, h,
                                                         text_fields, text_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_MTEXT)
            {
                MTEXT_CTX ctx;
                double x, y, z;
                common_w_defaults(&ctx.c);
                dwg_mtext_get_point(e, &x, &y, &z);
                ctx.ix = x; ctx.iy = y; ctx.iz = z;
                ctx.rect_width = dwg_mtext_get_rect_width(e);
                ctx.text_height = dwg_mtext_get_height(e);
                ctx.line_space = dwg_mtext_get_line_space(e);
                ctx.attach = dwg_mtext_get_attach(e);
                ctx.text = dwg_mtext_get_text(e);
                ctx.style = resolve_or_create_style(data, length, &objmap, dwg_mtext_get_style_name(e),
                                                    style_control_handle, have_style_control,
                                                    have_style_template, &style_tmpl,
                                                    new_styles, &n_new_styles, &next_handle,
                                                    obj_bytes, obj_lens, obj_handles, &n_objs);
                if (ctx.style == 0UL) { result = DWG_IO_ERROR_FORMAT; goto cleanup; }
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_MTEXT, h,
                                                         mtext_fields, mtext_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_SOLID)
            {
                SOLID_CTX ctx;
                DWG_SOLID3D *g = (DWG_SOLID3D *)e->geometry;
                common_w_defaults(&ctx.c);
                ctx.x1 = g->p1.x; ctx.y1 = g->p1.y; ctx.z1 = g->p1.z;
                ctx.x2 = g->p2.x; ctx.y2 = g->p2.y; ctx.z2 = g->p2.z;
                ctx.x3 = g->p3.x; ctx.y3 = g->p3.y; ctx.z3 = g->p3.z;
                ctx.x4 = g->p4.x; ctx.y4 = g->p4.y; ctx.z4 = g->p4.z;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_SOLID, h,
                                                         solid_fields, solid_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_INSERT)
            {
                INSERT_CTX ctx;
                double x, y, z, sx, sy, sz;
                unsigned long block_h;
                const char *block_name = dwg_insert_get_block_name(e);
                if (block_name == NULL ||
                    !find_table_record_handle(data, length, &objmap, (unsigned short)DWG_R2000_TYPE_BLOCK_HEADER,
                                              block_name, &block_h))
                {
                    result = DWG_IO_ERROR_FORMAT;
                    goto cleanup;
                }
                common_w_defaults(&ctx.c);
                dwg_insert_get_point(e, &x, &y, &z);
                dwg_insert_get_scale(e, &sx, &sy, &sz);
                ctx.ix = x; ctx.iy = y; ctx.iz = z;
                ctx.sx = sx; ctx.sy = sy; ctx.sz = sz;
                ctx.rotation_deg = dwg_insert_get_angle(e);
                ctx.block_header = block_h;
                ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                if (n_objs >= MAX_NEW_OBJECTS) { result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_INSERT, h,
                                                         insert_fields, insert_handles, &ctx, &obj_lens[n_objs]);
                obj_handles[n_objs] = h; n_objs++;
            }
            else if (t == DWG_ENTITY_POLYLINE)
            {
                HPOLYLINE pl = dwg_polyline_from_entity(e);
                HVERTEX v;
                unsigned long vcount, seqend_h, vi;
                unsigned long *vhandles;
                int is_3d;
                double first_z = 0.0;

                if (pl == NULL)
                    continue;

                /* VERTEX2D has no per-vertex Z of its own -- it's
                   always 0, relying entirely on the owning POLYLINE's
                   shared Elevation field (see vertex2d_fields, and
                   decode_vertex3d_and_advance's own comment in
                   dwg_r2000_entity_reader.c for the confirmed real-file
                   asymmetry between the two vertex kinds). Encoding
                   every polyline as 2D regardless of its actual vertex
                   data would silently flatten genuine 3D geometry to
                   one shared Z -- caught by code review, not a
                   hypothetical: dwg_polyline_add_vertex/add_vertex2
                   explicitly accept a real per-vertex z. Pick 3D
                   encoding the moment any vertex's Z differs from the
                   first vertex's -- 2D is used only when every vertex
                   genuinely shares one Z (matching real AutoCAD's own
                   choice of representation), and only 2D carries
                   bulge/width (3D has no room for them; see
                   polyline3d_fields/vertex3d_fields). */
                is_3d = 0;
                {
                    int have_first = 0;
                    for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
                    {
                        double vx, vy, vz;
                        dwg_vertex_get_point(v, &vx, &vy, &vz);
                        if (!have_first) { first_z = vz; have_first = 1; }
                        else if (vz != first_z) { is_3d = 1; break; }
                    }
                }

                vcount = dwg_polyline_vertex_count(pl);
                vhandles = (unsigned long *)calloc(vcount > 0UL ? vcount : 1UL, sizeof(unsigned long));
                if (vhandles == NULL)
                {
                    result = DWG_IO_ERROR_MEMORY;
                    goto cleanup;
                }
                for (vi = 0UL; vi < vcount; vi++)
                    vhandles[vi] = next_handle++;
                seqend_h = next_handle++;

                if (n_objs >= MAX_NEW_OBJECTS) { free(vhandles); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                if (is_3d)
                {
                    POLYLINE3D_CTX ctx;
                    common_w_defaults(&ctx.c);
                    ctx.closed = dwg_polyline_is_closed(pl);
                    ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                    ctx.first_vertex = vcount > 0UL ? vhandles[0] : 0UL;
                    ctx.last_vertex = vcount > 0UL ? vhandles[vcount - 1UL] : 0UL;
                    ctx.seqend = seqend_h;
                    obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_POLYLINE3D, h,
                                                             polyline3d_fields, polyline3d_handles, &ctx, &obj_lens[n_objs]);
                }
                else
                {
                    POLYLINE2D_CTX ctx;
                    common_w_defaults(&ctx.c);
                    ctx.elevation = dwg_polyline_get_elevation(pl);
                    ctx.closed = dwg_polyline_is_closed(pl);
                    ctx.layer = layer_h; ctx.prev = prev; ctx.next = next;
                    ctx.first_vertex = vcount > 0UL ? vhandles[0] : 0UL;
                    ctx.last_vertex = vcount > 0UL ? vhandles[vcount - 1UL] : 0UL;
                    ctx.seqend = seqend_h;
                    obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_POLYLINE2D, h,
                                                             polyline2d_fields, polyline2d_handles, &ctx, &obj_lens[n_objs]);
                }
                obj_handles[n_objs] = h; n_objs++;

                vi = 0UL;
                for (v = dwg_polyline_first_vertex(pl); v != NULL; v = dwg_polyline_next_vertex(v))
                {
                    double vx, vy, vz;
                    unsigned long vprev = (vi > 0UL) ? vhandles[vi - 1UL] : 0UL;
                    unsigned long vnext = (vi + 1UL < vcount) ? vhandles[vi + 1UL] : 0UL;
                    dwg_vertex_get_point(v, &vx, &vy, &vz);

                    if (n_objs >= MAX_NEW_OBJECTS) { free(vhandles); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                    if (is_3d)
                    {
                        VERTEX3D_CTX vctx;
                        common_w_defaults(&vctx.c);
                        vctx.c.entmode = 0UL;
                        vctx.x = vx; vctx.y = vy; vctx.z = vz;
                        vctx.owner = h;
                        vctx.layer = layer_h;
                        vctx.prev = vprev; vctx.next = vnext;
                        obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_VERTEX3D, vhandles[vi],
                                                                 vertex3d_fields, vertex3d_handles, &vctx, &obj_lens[n_objs]);
                    }
                    else
                    {
                        VERTEX2D_CTX vctx;
                        common_w_defaults(&vctx.c);
                        vctx.c.entmode = 0UL;
                        vctx.x = vx; vctx.y = vy;
                        vctx.bulge = dwg_vertex_get_bulge(v);
                        vctx.start_width = v->start_width;
                        vctx.end_width = v->end_width;
                        vctx.owner = h;
                        vctx.layer = layer_h;
                        vctx.prev = vprev; vctx.next = vnext;
                        obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_VERTEX2D, vhandles[vi],
                                                                 vertex2d_fields, vertex2d_handles, &vctx, &obj_lens[n_objs]);
                    }
                    obj_handles[n_objs] = vhandles[vi]; n_objs++;
                    vi++;
                }

                {
                    SEQEND_CTX sctx;
                    common_w_defaults(&sctx.c);
                    sctx.c.entmode = 0UL;
                    sctx.owner = h;
                    sctx.layer = layer_h;
                    sctx.prev = 0UL;
                    sctx.next = 0UL;
                    if (n_objs >= MAX_NEW_OBJECTS) { free(vhandles); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
                    obj_bytes[n_objs] = r2000_encode_object((unsigned short)DWG_R2000_TYPE_SEQEND, seqend_h,
                                                             seqend_fields, seqend_handles, &sctx, &obj_lens[n_objs]);
                    obj_handles[n_objs] = seqend_h; n_objs++;
                }

                free(vhandles);
            }
            /* other types: not yet modeled by this writer (or this
               engine's own reader either) -- silently skipped, same
               "trust the framing, skip what isn't understood"
               convention the reader uses. */
        }
    }

    /* check every encode succeeded */
    for (i = 0UL; i < n_objs; i++)
    {
        if (obj_bytes[i] == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
    }

    /* BLOCK_HEADER replacement, with new first/last-entity */
    {
        BLOCK_HEADER_CTX bhctx;
        bhctx.name = bh.name;
        bhctx.flags_bits = bh.flags_bits;
        bhctx.xdic = bh.xdic;
        bhctx.owner = bh.owner;
        bhctx.reserved = bh.reserved;
        bhctx.block_entity = bh.block_entity;
        bhctx.first_entity = n_top > 0UL ? top_handles[0] : 0UL;
        bhctx.last_entity = n_top > 0UL ? top_handles[n_top - 1UL] : 0UL;
        bhctx.endblk = bh.endblk;
        bhctx.layout = bh.layout;
        new_bh_bytes = r2000_encode_object((unsigned short)DWG_R2000_TYPE_BLOCK_HEADER, model_space_handle,
                                           block_header_fields, block_header_handles, &bhctx, &new_bh_len);
        if (new_bh_bytes == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
    }

    /* LAYER_CONTROL replacement, only if this write actually created
       new layers -- rewritten with the entry count bumped and the new
       LAYER handle(s) appended. */
    if (n_new_layers > 0UL)
    {
        LAYER_CONTROL_CTX lcctx;
        unsigned long combined_count = lc_raw.count + n_new_layers;
        unsigned long *combined;
        unsigned long dummy;

        /* get_layer_control_raw rejects any count > 255 (its entries[]
           is a fixed [256]) -- if this write pushed the real count past
           that, a later write using this output as its own template
           would silently fail to find a usable LAYER_CONTROL/template
           LAYER, so reject here instead of producing an unusable file. */
        if (combined_count > 255UL)
        {
            result = DWG_IO_ERROR_FORMAT;
            goto cleanup;
        }
        combined = (unsigned long *)malloc(combined_count * sizeof(unsigned long));
        if (combined == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        for (i = 0UL; i < lc_raw.count; i++)
            combined[i] = lc_raw.entries[i];
        for (i = 0UL; i < n_new_layers; i++)
            combined[lc_raw.count + i] = new_layers[i].handle;

        lcctx.owner = lc_raw.owner;
        lcctx.xdic = lc_raw.xdic;
        lcctx.count = combined_count;
        lcctx.entries = combined;
        new_lc_bytes = r2000_encode_object((unsigned short)DWG_R2000_TYPE_LAYER_CONTROL, layer_control_handle,
                                           layer_control_fields, layer_control_handles, &lcctx, &new_lc_len);
        free(combined);
        if (new_lc_bytes == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        get_object_span(data, length, layer_control_loc, &old_lc_start, &dummy);
        old_lc_end = dummy;
    }

    /* STYLE_CONTROL replacement, same idea, only if new styles were
       created. */
    if (n_new_styles > 0UL)
    {
        STYLE_CONTROL_CTX scctx;
        unsigned long combined_count = sc_raw.count + n_new_styles;
        unsigned long *combined;
        unsigned long dummy;

        if (combined_count > 255UL)
        {
            result = DWG_IO_ERROR_FORMAT;
            goto cleanup;
        }
        combined = (unsigned long *)malloc(combined_count * sizeof(unsigned long));
        if (combined == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        for (i = 0UL; i < sc_raw.count; i++)
            combined[i] = sc_raw.entries[i];
        for (i = 0UL; i < n_new_styles; i++)
            combined[sc_raw.count + i] = new_styles[i].handle;

        scctx.owner = sc_raw.owner;
        scctx.xdic = sc_raw.xdic;
        scctx.count = combined_count;
        scctx.entries = combined;
        new_sc_bytes = r2000_encode_object((unsigned short)DWG_R2000_TYPE_STYLE_CONTROL, style_control_handle,
                                           style_control_fields, style_control_handles, &scctx, &new_sc_len);
        free(combined);
        if (new_sc_bytes == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        get_object_span(data, length, style_control_loc, &old_sc_start, &dummy);
        old_sc_end = dummy;
    }

    /* splice: apply every replacement region in file-position order.
       New content (new entities, new LAYER objects) isn't its own
       region -- it's all appended into the entities region's
       replacement bytes, since object position is arbitrary and only
       the object map's handle->location entries matter. */
    {
        unsigned long total_new_entities_len = 0UL;
        unsigned char *entities_blob;
        unsigned long off, cursor;
        unsigned long cap;

        for (i = 0UL; i < n_objs; i++)
            total_new_entities_len += obj_lens[i];
        entities_blob = (unsigned char *)malloc(total_new_entities_len > 0UL ? total_new_entities_len : 1UL);
        if (entities_blob == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        off = 0UL;
        for (i = 0UL; i < n_objs; i++)
        {
            memcpy(entities_blob + off, obj_bytes[i], obj_lens[i]);
            off += obj_lens[i];
        }

        /* MAX_REGIONS is sized to exactly today's 4 possible regions
           (BLOCK_HEADER + entities always, LAYER_CONTROL/STYLE_CONTROL
           conditionally) -- these checks exist so that a future 5th
           independently-creatable table type added by copy-pasting this
           pattern fails cleanly here instead of silently overflowing
           the fixed-size `regions` array. */
        if (n_regions >= MAX_REGIONS) { free(entities_blob); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
        regions[n_regions].start = old_bh_start; regions[n_regions].end = old_bh_end;
        regions[n_regions].bytes = new_bh_bytes; regions[n_regions].len = new_bh_len;
        n_regions++;
        if (n_regions >= MAX_REGIONS) { free(entities_blob); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
        regions[n_regions].start = old_entities_start; regions[n_regions].end = old_entities_end;
        regions[n_regions].bytes = entities_blob; regions[n_regions].len = total_new_entities_len;
        n_regions++;
        if (n_new_layers > 0UL)
        {
            if (n_regions >= MAX_REGIONS) { free(entities_blob); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
            regions[n_regions].start = old_lc_start; regions[n_regions].end = old_lc_end;
            regions[n_regions].bytes = new_lc_bytes; regions[n_regions].len = new_lc_len;
            n_regions++;
        }
        if (n_new_styles > 0UL)
        {
            if (n_regions >= MAX_REGIONS) { free(entities_blob); result = DWG_IO_ERROR_MEMORY; goto cleanup; }
            regions[n_regions].start = old_sc_start; regions[n_regions].end = old_sc_end;
            regions[n_regions].bytes = new_sc_bytes; regions[n_regions].len = new_sc_len;
            n_regions++;
        }
        qsort(regions, n_regions, sizeof(REGION), region_cmp);

        cap = length + new_bh_len + new_lc_len + new_sc_len + total_new_entities_len + 16UL;
        out_buf = (unsigned char *)malloc(cap);
        if (out_buf == NULL)
        {
            free(entities_blob);
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }

        cursor = 0UL;
        {
            long cum_shift = 0L;
            for (i = 0UL; i < n_regions; i++)
            {
                memcpy(out_buf + out_len, data + cursor, regions[i].start - cursor);
                out_len += regions[i].start - cursor;

                memcpy(out_buf + out_len, regions[i].bytes, regions[i].len);
                out_len += regions[i].len;

                cum_shift += (long)regions[i].len - (long)(regions[i].end - regions[i].start);
                region_boundaries[i] = regions[i].end;
                region_shifts[i] = cum_shift;
                cursor = regions[i].end;
            }
        }
        memcpy(out_buf + out_len, data + cursor, length - cursor);
        out_len += length - cursor;

        free(entities_blob);
    }

    /* build the new handle->location map */
    {
        unsigned long entity_base_loc = old_entities_start +
            region_shift_at(old_entities_start, region_boundaries, region_shifts, n_regions);
        unsigned long running = 0UL;

        pairs = (HL_PAIR *)malloc((objmap.count + n_objs + 1UL) * sizeof(HL_PAIR));
        if (pairs == NULL)
        {
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }

        for (i = 0UL; i < objmap.count; i++)
        {
            unsigned long h = objmap.entries[i].handle;
            unsigned long loc = objmap.entries[i].location;
            unsigned long ri;
            int replaced = 0;

            if (h == model_space_handle)
            {
                pairs[n_pairs].handle = h;
                pairs[n_pairs].location = old_bh_start +
                    region_shift_at(old_bh_start, region_boundaries, region_shifts, n_regions);
                n_pairs++;
                continue;
            }
            if (n_new_layers > 0UL && h == layer_control_handle)
            {
                pairs[n_pairs].handle = h;
                pairs[n_pairs].location = old_lc_start +
                    region_shift_at(old_lc_start, region_boundaries, region_shifts, n_regions);
                n_pairs++;
                continue;
            }
            if (n_new_styles > 0UL && h == style_control_handle)
            {
                pairs[n_pairs].handle = h;
                pairs[n_pairs].location = old_sc_start +
                    region_shift_at(old_sc_start, region_boundaries, region_shifts, n_regions);
                n_pairs++;
                continue;
            }

            /* skip whatever old content any region just replaced */
            for (ri = 0UL; ri < n_regions; ri++)
            {
                if (loc >= regions[ri].start && loc < regions[ri].end)
                {
                    replaced = 1;
                    break;
                }
            }
            if (replaced)
                continue;

            pairs[n_pairs].handle = h;
            pairs[n_pairs].location = loc + region_shift_at(loc, region_boundaries, region_shifts, n_regions);
            n_pairs++;
        }

        for (i = 0UL; i < n_objs; i++)
        {
            pairs[n_pairs].handle = obj_handles[i];
            pairs[n_pairs].location = entity_base_loc + running;
            running += obj_lens[i];
            n_pairs++;
        }
    }

    qsort(pairs, n_pairs, sizeof(HL_PAIR), hl_pair_cmp);

    /* object map regeneration */
    {
        DWG_BW section;
        unsigned char *section_bytes = NULL;
        unsigned long section_len = 0UL, section_size_field;
        unsigned short crc;
        unsigned char size_be[2], crc_be[2];
        unsigned char *objmap_bytes;
        unsigned long objmap_total_len;
        unsigned long last_handle = 0UL, last_loc = 0UL;
        unsigned long new_objmap_seeker, new_objmap_size;
        const DWG_R2000_SECTION_RECORD *rec4;
        unsigned long new_rec4_seeker = 0UL;
        unsigned char measurement[4];
        int have_rec4 = 0;

        bw_init(&section);
        for (i = 0UL; i < n_pairs; i++)
        {
            bw_write_mc(&section, (long)(pairs[i].handle - last_handle), 0);
            bw_write_mc(&section, (long)pairs[i].location - (long)last_loc, 1);
            last_handle = pairs[i].handle;
            last_loc = pairs[i].location;
        }
        bw_align_to_byte(&section);
        if (section.failed)
        {
            bw_free(&section);
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        section_len = section.bit_count / 8UL;

        section_size_field = 2UL + section_len;
        if (section_size_field > 0xFFFFUL)
        {
            bw_free(&section);
            result = DWG_IO_ERROR_FORMAT;
            goto cleanup;
        }

        section_bytes = (unsigned char *)malloc(section_size_field + 2UL);
        if (section_bytes == NULL)
        {
            bw_free(&section);
            result = DWG_IO_ERROR_MEMORY;
            goto cleanup;
        }
        size_be[0] = (unsigned char)((section_size_field >> 8) & 0xFFU);
        size_be[1] = (unsigned char)(section_size_field & 0xFFU);
        memcpy(section_bytes, size_be, 2UL);
        memcpy(section_bytes + 2UL, section.data, section_len);
        bw_free(&section);

        crc = dwg_r2000_crc(0xC0C1U, section_bytes, section_size_field);
        crc_be[0] = (unsigned char)((crc >> 8) & 0xFFU);
        crc_be[1] = (unsigned char)(crc & 0xFFU);
        memcpy(section_bytes + section_size_field, crc_be, 2UL);

        /* terminator sub-section */
        {
            unsigned char term[4];
            unsigned short term_crc;
            term[0] = 0U; term[1] = 2U;
            term_crc = dwg_r2000_crc(0xC0C1U, term, 2UL);
            term[2] = (unsigned char)((term_crc >> 8) & 0xFFU);
            term[3] = (unsigned char)(term_crc & 0xFFU);

            objmap_total_len = section_size_field + 2UL + 4UL;
            objmap_bytes = (unsigned char *)malloc(objmap_total_len);
            if (objmap_bytes == NULL)
            {
                free(section_bytes);
                result = DWG_IO_ERROR_MEMORY;
                goto cleanup;
            }
            memcpy(objmap_bytes, section_bytes, section_size_field + 2UL);
            memcpy(objmap_bytes + section_size_field + 2UL, term, 4UL);
            free(section_bytes);
        }

        new_objmap_seeker = objmap_rec->seeker +
            region_shift_at(objmap_rec->seeker, region_boundaries, region_shifts, n_regions);
        new_objmap_size = objmap_total_len;

        rec4 = dwg_r2000_find_record(&hdr, 4);
        if (rec4 != NULL && rec4->size == 4UL)
        {
            memcpy(measurement, data + rec4->seeker, 4UL);
            new_rec4_seeker = new_objmap_seeker + new_objmap_size;
            have_rec4 = 1;
        }

        /* truncate output at new_objmap_seeker, append the new map,
           then (if present) the 4-byte MEASUREMENT tail -- dropping
           the rest of the template's trailing "second header" region,
           same documented first-milestone scope limit as the Python
           prototype. */
        {
            unsigned long final_len = new_objmap_seeker + new_objmap_size + (have_rec4 ? 4UL : 0UL);
            if (final_len > out_len + objmap_total_len + 8UL)
            {
                /* shouldn't happen given out_buf's capacity margin, but
                   guard rather than overrun */
                free(objmap_bytes);
                result = DWG_IO_ERROR_MEMORY;
                goto cleanup;
            }
            memcpy(out_buf + new_objmap_seeker, objmap_bytes, objmap_total_len);
            if (have_rec4)
                memcpy(out_buf + new_objmap_seeker + new_objmap_size, measurement, 4UL);
            out_len = final_len;
        }
        free(objmap_bytes);

        /* patch header section-locator records + header CRC (all
           before any replacement region, so their absolute offsets in
           out_buf match the original file exactly) */
        {
            unsigned long off = 0x19UL; /* records always start at 0x19 */
            unsigned long recno_i;
            unsigned short xor_const;
            unsigned short header_crc;
            unsigned long crc_off;

            for (recno_i = 0UL; recno_i < hdr.record_count; recno_i++)
            {
                unsigned long rec_off = off + recno_i * 9UL;
                unsigned char recno = out_buf[rec_off];
                if (recno == 2U)
                {
                    out_buf[rec_off + 1UL] = (unsigned char)(new_objmap_seeker & 0xFFUL);
                    out_buf[rec_off + 2UL] = (unsigned char)((new_objmap_seeker >> 8) & 0xFFUL);
                    out_buf[rec_off + 3UL] = (unsigned char)((new_objmap_seeker >> 16) & 0xFFUL);
                    out_buf[rec_off + 4UL] = (unsigned char)((new_objmap_seeker >> 24) & 0xFFUL);
                    out_buf[rec_off + 5UL] = (unsigned char)(new_objmap_size & 0xFFUL);
                    out_buf[rec_off + 6UL] = (unsigned char)((new_objmap_size >> 8) & 0xFFUL);
                    out_buf[rec_off + 7UL] = (unsigned char)((new_objmap_size >> 16) & 0xFFUL);
                    out_buf[rec_off + 8UL] = (unsigned char)((new_objmap_size >> 24) & 0xFFUL);
                }
                else if (recno == 4U && have_rec4)
                {
                    out_buf[rec_off + 1UL] = (unsigned char)(new_rec4_seeker & 0xFFUL);
                    out_buf[rec_off + 2UL] = (unsigned char)((new_rec4_seeker >> 8) & 0xFFUL);
                    out_buf[rec_off + 3UL] = (unsigned char)((new_rec4_seeker >> 16) & 0xFFUL);
                    out_buf[rec_off + 4UL] = (unsigned char)((new_rec4_seeker >> 24) & 0xFFUL);
                }
            }

            crc_off = off + hdr.record_count * 9UL;
            switch (hdr.record_count)
            {
            case 3UL: xor_const = 0xA598U; break;
            case 4UL: xor_const = 0x8101U; break;
            case 5UL: xor_const = 0x3CC4U; break;
            case 6UL: xor_const = 0x8461U; break;
            default: xor_const = 0U; break;
            }
            header_crc = (unsigned short)(dwg_r2000_crc(0U, out_buf, crc_off) ^ xor_const);
            out_buf[crc_off] = (unsigned char)(header_crc & 0xFFU);
            out_buf[crc_off + 1UL] = (unsigned char)((header_crc >> 8) & 0xFFU);
        }
    }

    outfp = fopen(out_path, "wb");
    if (outfp == NULL)
    {
        result = DWG_IO_ERROR_OPEN;
        goto cleanup;
    }
    if (fwrite(out_buf, 1, (size_t)out_len, outfp) != (size_t)out_len)
    {
        fclose(outfp);
        result = DWG_IO_ERROR_OPEN;
        goto cleanup;
    }
    fclose(outfp);

    result = DWG_IO_OK;

cleanup:
    if (new_bh_bytes != NULL) free(new_bh_bytes);
    if (new_lc_bytes != NULL) free(new_lc_bytes);
    if (new_sc_bytes != NULL) free(new_sc_bytes);
    if (obj_bytes != NULL)
    {
        for (i = 0UL; i < n_objs; i++)
            if (obj_bytes[i] != NULL) free(obj_bytes[i]);
        free(obj_bytes);
    }
    if (obj_lens != NULL) free(obj_lens);
    if (obj_handles != NULL) free(obj_handles);
    if (top_handles != NULL) free(top_handles);
    if (new_layers != NULL) free(new_layers);
    if (new_styles != NULL) free(new_styles);
    if (pairs != NULL) free(pairs);
    if (out_buf != NULL) free(out_buf);
    dwg_r2000_objmap_free(&objmap);
    free(data);

    return result;
}
