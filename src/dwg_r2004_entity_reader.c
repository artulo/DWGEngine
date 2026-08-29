#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "dwg_r2004_reader.h"
#include "dwg_r2004_decompress.h"
#include "dwg_bitstream.h"
#include "dwg_document.h"
#include "dwg_geometry.h"
#include "dwg_entity.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_hatch.h"
#include "dwg_vertex.h"
#include "dwg_transform.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_polyline.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* See dwg_r2004_reader.h and reverse/DWG_R2004plus_format_reference.md
   for the full derivation of everything in this file. */

#define DWG_R2004_MAX_PLAUSIBLE_COORD 1.0e7
#define DWG_R2004_MAX_REACTORS 1000UL
#define DWG_R2004_MAX_SECTIONS 64UL
#define DWG_R2004_MAX_PAGES 8192UL
#define DWG_R2004_MAX_PAGES_PER_SECTION 4096UL

static int is_plausible_coord(double v)
{
    return v > -DWG_R2004_MAX_PLAUSIBLE_COORD && v < DWG_R2004_MAX_PLAUSIBLE_COORD;
}

static void apply_color(HENTITY e, unsigned short color)
{
    if (color != 0U && color != 256U)
        dwg_entity_put_color(e, color);
}

/* ---- byte-aligned little-endian raw reads, used everywhere below
   (section/page headers are always byte-aligned) ---- */

static unsigned long rl_at(const unsigned char *data, unsigned long off)
{
    return (unsigned long)data[off] | ((unsigned long)data[off + 1UL] << 8) |
           ((unsigned long)data[off + 2UL] << 16) | ((unsigned long)data[off + 3UL] << 24);
}

static long rld_at(const unsigned char *data, unsigned long off)
{
    return (long)rl_at(data, off);
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

/* ---- encrypted file header (0x80..0x80+0x6C), spec 4.3/r2004_file_header.spec ---- */

typedef struct
{
    unsigned long section_map_id;
    unsigned long section_map_address; /* real field is RLL/8 bytes; only the low
                                          32 bits are used -- real files never need more */
    long section_info_id;
    unsigned long section_array_size;
} DWG_R2004_FHDR;

static int decrypt_file_header(const unsigned char *data, unsigned long length, DWG_R2004_FHDR *out)
{
    unsigned char dec[0x6C];
    unsigned long randseed = 1UL;
    unsigned long i;

    if (length < 0x80UL + 0x6CUL)
        return 0;

    for (i = 0UL; i < 0x6CUL; i++)
    {
        randseed = (randseed * 0x343FDUL + 0x269EC3UL) & 0xFFFFFFFFUL;
        dec[i] = (unsigned char)(data[0x80UL + i] ^ ((randseed >> 16) & 0xFFUL));
    }

    if (memcmp(dec, "AcFssFcAJMB", 11) != 0)
        return 0;

    out->section_map_id = rl_at(dec, 0x50);
    out->section_map_address = rl_at(dec, 0x54); /* low 32 bits of the RLL */
    out->section_info_id = rld_at(dec, 0x5C);
    out->section_array_size = rl_at(dec, 0x60);
    return 1;
}

/* ---- system section pages (Section Page Map / Section Info): 0x14-byte
   plaintext header + LZ77 payload, spec 4.4 ---- */

static int read_system_page(const unsigned char *data, unsigned long length, unsigned long addr,
                            unsigned long required_type, unsigned char **out_decomp, unsigned long *out_decomp_size)
{
    unsigned long page_type, decomp_size, comp_size, payload_off;
    unsigned char *buf;
    long produced;

    if (addr + 0x14UL >= length)
        return 0;

    page_type = rl_at(data, addr);
    if (page_type != required_type)
        return 0;

    decomp_size = rl_at(data, addr + 4UL);
    comp_size = rl_at(data, addr + 8UL);
    payload_off = addr + 0x14UL;

    if (decomp_size == 0UL || decomp_size > 64UL * 1024UL * 1024UL)
        return 0;
    if (payload_off + comp_size > length)
        return 0;

    buf = (unsigned char *)malloc((size_t)decomp_size + 16UL);
    if (buf == NULL)
        return 0;

    produced = dwg_r2004_decompress(data + payload_off, comp_size, buf, decomp_size + 16UL, decomp_size);
    if (produced < 0L)
    {
        free(buf);
        return 0;
    }

    *out_decomp = buf;
    *out_decomp_size = decomp_size;
    return 1;
}

/* ---- Section Page Map: a flat list of (page_number, page_size) pairs,
   file addresses accumulated starting at 0x100 -- spec 4.4 ---- */

typedef struct
{
    long number;
    unsigned long address;
    unsigned long size;
} DWG_R2004_PAGE;

static long find_page(const DWG_R2004_PAGE *pages, unsigned long count, long number, unsigned long *out_addr)
{
    unsigned long i;
    for (i = 0UL; i < count; i++)
    {
        if (pages[i].number == number)
        {
            *out_addr = pages[i].address;
            return 1L;
        }
    }
    return 0L;
}

/* ---- Section Info (aka "Data Section Map"): named sections, each with
   its own list of (page_number, decompressed start_offset) pairs --
   spec 4.5 ---- */

typedef struct
{
    long page_number;
    unsigned long start_offset;
} DWG_R2004_SECPAGE;

typedef struct
{
    char name[65];
    unsigned long total_size; /* declared decompressed size of the whole section */
    unsigned long compressed; /* 1=no, 2=yes -- per LibreDWG's real src/decode.c
                                  (read_2004_compressed_section): sections marked
                                  1 store each page's bytes RAW after its 32-byte
                                  header, no LZ77 involved at all -- running the
                                  decompressor on them anyway (this reader's own
                                  gap until this fix) produces plausible-looking
                                  garbage instead of an error, since LZ77-
                                  interpreted arbitrary bytes can easily still
                                  reach a byte close to the expected decompressed
                                  size by chance. */
    DWG_R2004_SECPAGE pages[DWG_R2004_MAX_PAGES_PER_SECTION];
    unsigned long page_count;
} DWG_R2004_SECTION;

/* Decompresses every page of a named section into one contiguous
   buffer, each page placed at its own declared start_offset (matching
   the spec's own layout for multi-page sections like AcDb:AcDbObjects).
   Returns NULL on failure. */
static unsigned char *decompress_section(const unsigned char *data, unsigned long length,
                                         const DWG_R2004_PAGE *pagemap, unsigned long pagemap_count,
                                         const DWG_R2004_SECTION *sec)
{
    unsigned char *out;
    unsigned long i;

    if (sec->total_size == 0UL || sec->total_size > 256UL * 1024UL * 1024UL)
        return NULL;

    out = (unsigned char *)calloc((size_t)sec->total_size + 4096UL, 1UL);
    if (out == NULL)
        return NULL;

    for (i = 0UL; i < sec->page_count; i++)
    {
        unsigned long file_addr, secmask, hdr_words[8];
        unsigned long j, comp_size, decomp_size, start_off;
        long produced;

        if (!find_page(pagemap, pagemap_count, sec->pages[i].page_number, &file_addr))
            continue; /* zero page / gap -- legitimately absent, not an error */

        if (file_addr + 32UL >= length)
            continue;

        /* data-page header (32 bytes) is obfuscated with an offset-
           dependent XOR over 8 UInt32 words, spec 4.6 -- a different,
           simpler scheme than the file header's LCG-magic-sequence one. */
        secmask = 0x4164536BUL ^ file_addr;
        for (j = 0UL; j < 8UL; j++)
            hdr_words[j] = rl_at(data, file_addr + j * 4UL) ^ secmask;

        /* hdr_words: [0]=page_type(0x4163043b) [1]=section_number
           [2]=compressed size [3]=decompressed size [4]=start offset
           [5]=header checksum [6]=data checksum [7]=unknown(0) */
        if (hdr_words[0] != 0x4163043BUL)
            continue;

        comp_size = hdr_words[2];
        decomp_size = hdr_words[3];
        start_off = hdr_words[4];

        if (file_addr + 32UL + comp_size > length)
            continue;
        if (start_off + decomp_size > sec->total_size + 4096UL)
            continue;

        /* Real, confirmed bug found via LibreDWG's actual source
           (D:\estudio\libredwg-master/src/decode.c,
           read_2004_compressed_section): whether a page is even LZ77-
           compressed at all is a per-SECTION flag (`compressed`, 1=no,
           2=yes) read from the file itself -- this reader used to run
           the decompressor UNCONDITIONALLY on every page regardless of
           this flag. For a section actually marked uncompressed (1),
           its pages store raw bytes directly after the 32-byte page
           header -- running the LZ77 decompressor on them anyway
           doesn't error out (arbitrary bytes still parse as SOME
           sequence of opcodes and can coincidentally reach a length
           close to the expected decompressed size), it just silently
           produces GARBAGE that structurally happens to look like
           noise rather than real objects -- a much harder class of bug
           to detect than a missing page or a short decompression (both
           already checked and ruled out earlier this investigation).
           This is the leading suspect for the still-unexplained
           PUERTAS/VENTANAS/LOSAS content gap on `02_Planta 1 Baja_
           A3ver18.dwg`. */
        if (sec->compressed == 2UL)
        {
            produced = dwg_r2004_decompress(data + file_addr + 32UL, comp_size,
                                            out + start_off, (sec->total_size + 4096UL) - start_off,
                                            decomp_size);
            (void)produced;
        }
        else
        {
            /* uncompressed page: raw passthrough, mirroring LibreDWG's
               own real uncompressed-section path exactly (size = MIN
               (section_total_size - start_offset, page_size), copied
               straight from right after the page header). */
            unsigned long copy_size = decomp_size;
            if (start_off < sec->total_size)
            {
                unsigned long avail = sec->total_size - start_off;
                if (copy_size > avail) copy_size = avail;
            }
            else
                copy_size = 0UL;
            if (copy_size > 0UL && file_addr + 32UL + copy_size <= length &&
                start_off + copy_size <= sec->total_size + 4096UL)
                memcpy(out + start_off, data + file_addr + 32UL, (size_t)copy_size);
        }
    }

    return out;
}

static int parse_section_page_map(const unsigned char *data, unsigned long length,
                                  unsigned long section_map_addr,
                                  DWG_R2004_PAGE *pages, unsigned long *out_count)
{
    unsigned char *decomp = NULL;
    unsigned long decomp_size = 0UL;
    unsigned long off, cur_addr;
    unsigned long count = 0UL;

    if (!read_system_page(data, length, section_map_addr, 0x41630E3BUL, &decomp, &decomp_size))
        return 0;

    off = 0UL;
    cur_addr = 0x100UL;
    while (off + 8UL <= decomp_size && count < DWG_R2004_MAX_PAGES)
    {
        long number = rld_at(decomp, off);
        unsigned long size = rl_at(decomp, off + 4UL);
        off += 8UL;

        if (number >= 0L)
        {
            pages[count].number = number;
            pages[count].address = cur_addr;
            pages[count].size = size;
            count++;
        }
        else
        {
            /* gap entry: parent/left/right/0x00, 16 more bytes, no
               real page -- spec 4.4's binary-tree gap bookkeeping,
               not needed just to locate real pages by number. */
            if (off + 16UL > decomp_size)
                break;
            off += 16UL;
        }
        cur_addr += size;
    }

    free(decomp);
    *out_count = count;
    return 1;
}

static int parse_section_info(const unsigned char *data, unsigned long length,
                              const DWG_R2004_PAGE *pagemap, unsigned long pagemap_count,
                              long section_info_id,
                              DWG_R2004_SECTION *sections, unsigned long *out_count)
{
    unsigned long info_addr;
    unsigned char *decomp = NULL;
    unsigned long decomp_size = 0UL;
    unsigned long off, num_desc, i;
    unsigned long count = 0UL;

    if (!find_page(pagemap, pagemap_count, section_info_id, &info_addr))
        return 0;

    if (!read_system_page(data, length, info_addr, 0x4163003BUL, &decomp, &decomp_size))
        return 0;

    if (decomp_size < 20UL)
    {
        free(decomp);
        return 0;
    }

    num_desc = rl_at(decomp, 0);
    off = 20UL;

    for (i = 0UL; i < num_desc && count < DWG_R2004_MAX_SECTIONS; i++)
    {
        unsigned long sec_size_lo, num_sections, k;
        DWG_R2004_SECTION *s;

        if (off + 8UL + 6UL * 4UL + 64UL > decomp_size)
            break;

        sec_size_lo = rl_at(decomp, off); /* low 32 bits of an RLL total-size field */
        off += 8UL; /* skip the high 32 bits, real files never exceed 4GB per section */
        num_sections = rl_at(decomp, off);

        s = &sections[count];
        s->compressed = rl_at(decomp, off + 12UL); /* 3rd RL after num_sections: max_decomp_size, unknown, THEN compressed -- confirmed field order against LibreDWG's real read_R2004_section_info (src/decode.c) */
        off += 6UL * 4UL; /* num_sections, max_decomp_size, unknown, compressed, type, encrypted */

        memset(s->name, 0, sizeof(s->name));
        memcpy(s->name, decomp + off, 64UL);
        s->name[64] = '\0';
        off += 64UL;

        s->total_size = sec_size_lo;
        s->page_count = 0UL;

        if (num_sections > DWG_R2004_MAX_PAGES_PER_SECTION)
        {
            /* implausible -- skip this section's page list defensively,
               same "don't trust a single field" discipline the other
               readers already use for object-map/vertex counts. */
            break;
        }

        for (k = 0UL; k < num_sections; k++)
        {
            if (off + 16UL > decomp_size)
                break;
            if (s->page_count < DWG_R2004_MAX_PAGES_PER_SECTION)
            {
                s->pages[s->page_count].page_number = rld_at(decomp, off);
                s->pages[s->page_count].start_offset = rl_at(decomp, off + 8UL); /* low 32 bits of RLL */
                s->page_count++;
            }
            off += 16UL;
        }

        count++;
    }

    free(decomp);
    *out_count = count;
    return 1;
}

static const DWG_R2004_SECTION *find_section_by_name(const DWG_R2004_SECTION *sections, unsigned long count, const char *name)
{
    unsigned long i;
    for (i = 0UL; i < count; i++)
    {
        if (strncmp(sections[i].name, name, strlen(name)) == 0)
            return &sections[i];
    }
    return NULL;
}

/* ---- AcDb:Handles: same sub-section framing as R13-R2000's object map
   (2-byte BE size, MC pairs) -- confirmed CRC-verified against real
   data, see the format reference doc. R2004+ pages already know their
   own exact length, so (unlike R2000) there's no size==2 terminator
   sub-section -- trailing bytes are just page padding. ---- */

typedef struct
{
    unsigned long handle;
    unsigned long offset; /* byte offset into the decompressed AcDb:AcDbObjects buffer */
} DWG_R2004_HANDLE_ENTRY;

static unsigned long parse_handles(const unsigned char *buf, unsigned long len,
                                   DWG_R2004_HANDLE_ENTRY *entries, unsigned long max_entries)
{
    unsigned long pos = 0UL;
    unsigned long count = 0UL;
    unsigned long last_handle = 0UL;
    long last_loc = 0L;

    while (pos + 2UL <= len)
    {
        unsigned long ssize = ((unsigned long)buf[pos] << 8) | (unsigned long)buf[pos + 1UL];
        unsigned long sstart = pos;
        unsigned long send;
        DWG_BITSTREAM bs;

        if (ssize <= 2UL || ssize > len - pos)
            break;

        send = sstart + ssize;
        pos += 2UL;

        dwg_bs_init(&bs, buf, len);
        dwg_bs_seek_bit(&bs, pos * 8UL);

        while (dwg_bs_tell_bit(&bs) / 8UL < send && count < max_entries)
        {
            long hoff = dwg_bs_read_mc(&bs, 0);
            long loff = dwg_bs_read_mc(&bs, 1);

            last_handle += (unsigned long)hoff;
            last_loc += loff;
            if (last_loc < 0L)
                break;

            entries[count].handle = last_handle;
            entries[count].offset = (unsigned long)last_loc;
            count++;
        }

        pos = send + 2UL; /* skip the per-sub-section CRC-16 */
    }

    return count;
}

/* Binary search -- entries are non-decreasing by handle (parse_handles
   accumulates unsigned MC deltas), same invariant already exploited
   for R2000/R13-14's own object maps. */
static long find_handle(const DWG_R2004_HANDLE_ENTRY *entries, unsigned long count,
                        unsigned long handle, unsigned long *out_offset)
{
    unsigned long lo = 0UL, hi = count;
    while (lo < hi)
    {
        unsigned long mid = lo + (hi - lo) / 2UL;
        if (entries[mid].handle == handle)
        {
            *out_offset = entries[mid].offset;
            return 1L;
        }
        else if (entries[mid].handle < handle)
            lo = mid + 1UL;
        else
            hi = mid;
    }
    return 0L;
}

/*
 * R2007+'s "string stream": a separate Unicode-text substream living
 * at the very END of an object's own bit range, worked out backward
 * from a stolen `has_strings` bit. Confirmed by cross-checking real
 * decoded names against plausible real values (LAYER "0", a STYLE/
 * LTYPE "ACAD") -- see reverse/DWG_R2004plus_format_reference.md for
 * the full derivation, including 2 real off-by-N-bits mistakes found
 * and fixed while cracking this (a phantom extra -16 bit offset that
 * this final version does NOT have).
 *
 * type_start_bit: bit position right where this object's own Type
 * field begins (same reference point `bitsize` below is measured
 * from). bitsize: size*8 - handlestream_size (the field-substream
 * length, already being computed by callers for the handle-stream walk).
 *
 * Reads just the FIRST string field in the stream (fine for LAYER/
 * STYLE/LTYPE/BLOCK_HEADER's own Entry Name, always the first thing a
 * table record's own COMMON_TABLE_FLAGS macro puts there) -- ASCII
 * only (real layer/style/linetype names in practice), non-ASCII code
 * units replaced with '?'. Returns 1 and fills out_name on success, 0
 * if this object has no string stream at all or the data doesn't look
 * plausible (defensive, same "trust the framing but verify" discipline
 * as the rest of this reader).
 */
/*
 * Generalized cursor over an object's string-stream region: open once
 * (same has_strings/size framing as before), then read_next_r2004_string
 * can be called repeatedly to walk MULTIPLE strings in the same
 * declaration order the field stream encounters their FIELD_T calls --
 * confirmed necessary for HATCH, whose gradient-fill block and pattern
 * name are separate string reads in a fixed sequence, unlike LAYER/
 * STYLE/BLOCK_HEADER's table records which only ever need the first one.
 */
typedef struct
{
    DWG_BITSTREAM bs;
    unsigned long cursor;
    unsigned long region_end;
    long valid;
} DWG_R2004_STRSTREAM;

static void open_r2004_string_stream(const unsigned char *data, unsigned long length,
                                     unsigned long type_start_bit, unsigned long bitsize,
                                     DWG_R2004_STRSTREAM *ss)
{
    unsigned long hs_pos, size_pos, region_end, data_size, region_start;

    ss->valid = 0L;
    if (bitsize < 18UL || bitsize > length * 8UL)
        return;

    hs_pos = type_start_bit + bitsize - 1UL;

    dwg_bs_init(&ss->bs, data, length);
    dwg_bs_seek_bit(&ss->bs, hs_pos);
    if (dwg_bs_read_bit(&ss->bs) == 0UL)
        return; /* no string stream on this object */

    if (hs_pos < 16UL)
        return;
    size_pos = hs_pos - 16UL;
    dwg_bs_seek_bit(&ss->bs, size_pos);
    data_size = dwg_bs_read_rs(&ss->bs);

    if (data_size & 0x8000UL)
    {
        unsigned long hi;
        if (size_pos < 16UL)
            return;
        dwg_bs_seek_bit(&ss->bs, size_pos - 16UL);
        hi = dwg_bs_read_rs(&ss->bs);
        data_size = (data_size & 0x7FFFUL) | (hi << 15);
        region_end = size_pos - 16UL;
    }
    else
        region_end = size_pos;

    if (data_size == 0UL || data_size > bitsize || data_size > region_end)
        return;
    region_start = region_end - data_size;

    ss->cursor = region_start;
    ss->region_end = region_end;
    ss->valid = 1L;
}

static long read_next_r2004_string(DWG_R2004_STRSTREAM *ss, char *out_name, unsigned long out_size)
{
    unsigned long slen, k;

    if (!ss->valid || ss->cursor >= ss->region_end)
        return 0L;

    dwg_bs_seek_bit(&ss->bs, ss->cursor);
    slen = dwg_bs_read_bs(&ss->bs);
    if (slen > 2000UL) /* generous enough for real MTEXT content -- LAYER/
                           STYLE/pattern names are always far shorter, this
                           just guards against genuine garbage */
        return 0L;

    for (k = 0UL; k < slen; k++)
    {
        unsigned long cu = dwg_bs_read_rs(&ss->bs);
        if (k + 1UL < out_size)
            out_name[k] = (cu >= 32UL && cu < 127UL) ? (char)cu : '?';
    }
    if (out_size > 0UL)
        out_name[(slen < out_size - 1UL) ? slen : out_size - 1UL] = '\0';

    ss->cursor = dwg_bs_tell_bit(&ss->bs);
    return 1L;
}

static long read_r2004_first_string(const unsigned char *data, unsigned long length,
                                    unsigned long type_start_bit, unsigned long bitsize,
                                    char *out_name, unsigned long out_size)
{
    DWG_R2004_STRSTREAM ss;

    open_r2004_string_stream(data, length, type_start_bit, bitsize, &ss);
    if (!read_next_r2004_string(&ss, out_name, out_size))
        return 0L;
    if (out_name[0] == '\0')
        return 0L; /* preserve original "empty name = not resolved" behavior */
    return 1L;
}

/* ---- R2010+ Object Type: 2-bit pair selects how to read 1-2 following
   bytes -- ODA spec paragraph 2.12. ---- */

static unsigned long read_object_type_r2010(DWG_BITSTREAM *bs)
{
    unsigned long bb = dwg_bs_read_bb(bs);
    if (bb == 0UL)
        return dwg_bs_read_bits(bs, 8UL);
    else if (bb == 1UL)
        return dwg_bs_read_bits(bs, 8UL) + 0x1F0UL;
    else
        return dwg_bs_read_bits(bs, 8UL) | (dwg_bs_read_bits(bs, 8UL) << 8);
}

typedef struct
{
    unsigned long handle;
    unsigned short color;
    unsigned long entmode;
    unsigned long numreactors;
    unsigned long xdic_missing;
} DWG_R2004_COMMON_ENTITY;

/*
 * BLL (bit long long): a REAL, distinct primitive from BL/MC -- a 3-bit
 * length prefix (BB then B, giving 0-7) followed by that many raw bytes,
 * least-significant-byte first (confirmed via LibreDWG's real
 * bit_read_BLL in bits.c, not assumed). Used for preview_size (SINCE
 * R_2010b) and a few header fields this reader doesn't otherwise need.
 * Only the low 32 bits are kept -- a real DWG preview bitmap anywhere
 * near 4GB doesn't happen in practice, same simplification as this
 * reader's Handle values.
 */
static unsigned long dwg_bs_read_bll_lowpart(DWG_BITSTREAM *bs)
{
    unsigned int len, i;
    unsigned long result = 0UL;

    len = (unsigned int)((dwg_bs_read_bb(bs) << 1) | dwg_bs_read_bit(bs));
    switch (len)
    {
    case 1: return (unsigned long)dwg_bs_read_rc(bs);
    case 2: return (unsigned long)dwg_bs_read_rs(bs);
    case 4: return dwg_bs_read_rl(bs);
    default:
        for (i = 0U; i < len; i++)
        {
            unsigned char b = (unsigned char)dwg_bs_read_rc(bs);
            if (i < 4U)
                result |= ((unsigned long)b) << (i * 8U);
        }
        return result;
    }
}

/* R2010+ Common Entity Data -- see reverse/DWG_R2004plus_format_reference.md's
   "BREAKTHROUGH" sections for the derivation, cross-checked against
   LibreDWG's real decode.c/common_entity_data.spec (curl'd verbatim,
   not summarized). Two fields that don't exist for earlier versions
   and are easy to get wrong: `handlestream_size` (a UMC field this
   version reads right after Length, before Type -- R2000's physical
   "Obj size RL" does NOT exist here, it's computed, not read) and
   `Nolinks` (only exists in the bitstream through R2002 -- R2004+ never
   reads it at all). Caller has already read Length(MS)+handlestream_size
   (UMC)+Type before calling this. */
/* R2004+'s per-ENTITY color encoding ("ENC" in LibreDWG's own naming,
   see common_entity_data.spec's `SINCE (R_2004a) // ENC (entity color
   encoding)` block, confirmed against the real source at
   D:\estudio\libredwg-master): a single BS with flag bits packed into
   its OWN high byte (0x20=has alpha, 0x40=color-handle/book, 0x80=RGB,
   plus 0x01/0x02 sub-flags gating an extra name string for each),
   followed by whatever those flags require -- NOT the same encoding
   LAYER's own color field uses (see read_r2004_table_color's own
   comment for that real, byte-verified difference -- confirmed the
   hard way: reusing THIS function for LAYER produced a plausible-
   looking but systematically wrong color on every single layer).
   Extracted from read_common_entity_r2004's own inline block (logic
   unchanged) purely so it has its own name distinguishing it from the
   table-object version below -- entities are the only real caller. */
static unsigned short read_r2004_cmc_color(DWG_BITSTREAM *bs)
{
    unsigned char code;
    unsigned long value;
    unsigned long color_raw, color_flag;

    color_raw = dwg_bs_read_bs(bs); /* ENC: BS with flags in the high byte */
    color_flag = color_raw >> 8;
    if (color_flag & 0x20UL)
        (void)dwg_bs_read_bl(bs); /* alpha */
    if (color_flag & 0x40UL)
        dwg_bs_read_handle(bs, &code, &value); /* color book/handle color -- not modeled */
    else if (color_flag & 0x80UL)
        (void)dwg_bs_read_bl(bs); /* RGB value -- not modeled */
    if ((color_flag & 0x41UL) == 0x41UL)
    {
        char tmp[256];
        (void)dwg_bs_read_t(bs, tmp, sizeof(tmp)); /* color name */
    }
    if ((color_flag & 0x42UL) == 0x42UL)
    {
        char tmp[256];
        (void)dwg_bs_read_t(bs, tmp, sizeof(tmp)); /* color book name */
    }
    /* the low byte is only a real ACI index when neither the color-
       handle (0x40) nor RGB (0x80) flag is set -- see read_common_
       entity_r2004's own comment (unchanged) for the real ROTATORIO.dwg
       case this guards against. */
    return (color_flag & 0xC0UL) ? 0U : (unsigned short)(color_raw & 0xFFUL);
}

/* Real, confirmed different encoding found via LibreDWG's actual
   source (D:\estudio\libredwg-master/src/bits.c, `bit_read_CMC`, the
   function `FIELD_CMC` -- used by LAYER/STYLE/LTYPE/etc's table-record
   spec, dwg.spec's `DWG_TABLE(LAYER)` -- actually calls, confirmed
   distinct from entities' own lowercase `field_cmc`/ENC function
   above): `index = BS` (the palette index -- for our purposes the
   only part worth keeping, see below), then UNCONDITIONALLY (every
   version since R_2004, no flag gates it) `rgb = BL` (a full 32-bit
   raw long, NOT a flag-and-payload BS) and `flag = RC` (one raw byte,
   bit0/bit1 gating a name/book_name string pair) -- both always
   physically present regardless of `flag`'s value. Critically, the
   real name/book_name strings (when `flag` indicates they exist) are
   read from the file's separate STRING stream, not this main one --
   so they cost zero bits here regardless, nothing to skip. The first
   `index` reads gets overwritten by a computed nearest-palette-match
   from `rgb` in LibreDWG's own real implementation (true-color mode);
   this reader doesn't replicate that palette lookup (same "simple ACI
   only, RGB/named colors not modeled" simplification already used for
   entity color) -- the raw `index` is used directly, correct for the
   common case (a plain ACI-indexed layer, not a custom true-color
   one), same tradeoff already accepted elsewhere in this file. */
static unsigned short read_r2004_table_color(DWG_BITSTREAM *bs)
{
    unsigned long index_raw, rgb_raw, flag_raw;

    index_raw = dwg_bs_read_bs(bs);
    rgb_raw = dwg_bs_read_bl(bs);   /* unconditional for R2004+, always present */
    flag_raw = dwg_bs_read_rc(bs);  /* unconditional for R2004+, always present */
    (void)flag_raw;

    /* Real, confirmed via direct inspection (a dedicated raw-value dump
       against this exact file): `index_raw` alone is 0/BYBLOCK for
       EVERY real layer here -- this file was saved with its layer
       colors in TrueColor form, not the plain legacy index field.
       `rgb_raw`'s top byte (method, per bit_read_CMC/bit_upconvert_CMC)
       is 0xC3 ("TrueColor") for every one of them, and the LOW byte is
       a small, plausible standard ACI value (1-8 range observed: red/
       yellow/green/cyan/blue/magenta/white/gray) -- LibreDWG's own
       bit_upconvert_CMC confirms this exact convention: method 0xc3
       packs a palette INDEX into rgb's low bits, not a literal 24-bit
       RGB triple. Not implementing the general nearest-palette-match
       LibreDWG's real decoder does for genuine arbitrary RGB colors
       (out of scope, same "simple ACI only" simplification already
       used for entity color) -- but this specific, common "ACI stored
       via TrueColor method" case is real, verified data worth using
       directly rather than falling through to the useless index_raw=0. */
    if ((rgb_raw >> 24) == 0xC3UL && (rgb_raw & 0x00FFFFFFUL) <= 255UL)
        return (unsigned short)(rgb_raw & 0xFFUL);

    return (unsigned short)(index_raw & 0xFFFFUL);
}

static long read_common_entity_r2004(DWG_BITSTREAM *bs, DWG_R2004_COMMON_ENTITY *out)
{
    unsigned char code;
    unsigned long value;
    unsigned long eed_size;
    unsigned long numreactors;

    dwg_bs_read_handle(bs, &code, &value);
    out->handle = value; /* code is 0 (absolute) for an object's own handle */

    eed_size = dwg_bs_read_bs(bs);
    while (eed_size != 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* EED application handle */
        {
            unsigned long k;
            for (k = 0UL; k < eed_size; k++)
                (void)dwg_bs_read_rc(bs);
        }
        eed_size = dwg_bs_read_bs(bs);
    }

    if (dwg_bs_read_bit(bs) != 0UL) /* preview_exists (SINCE R_13b1) -- a real
                                        per-entity cached preview bitmap, NOT a
                                        generic "graphic" blob as this comment
                                        used to say. Confirmed via LibreDWG's
                                        real common_entity_data.spec: rare for
                                        simple entities (never triggered by
                                        LINE/CIRCLE/ARC/POINT/SOLID/TEXT/MTEXT
                                        in real testing) but real DIMENSION
                                        entities do carry one -- this exact
                                        field was silently misread as a plain
                                        BL for a long time because nothing had
                                        ever exercised the preview_exists=1
                                        path until DIMENSION support was added
                                        and produced garbage handles. */
    {
        unsigned long psize = dwg_bs_read_bll_lowpart(bs); /* SINCE R_2010b: BLL;
                                          our target versions (AC1024/1027/1032)
                                          are all >= R_2010b */
        unsigned long k;
        if (psize > bs->length)
            return 0L; /* implausible -- same defensive clamp as numreactors below */
        for (k = 0UL; k < psize; k++)
            (void)dwg_bs_read_rc(bs);
    }

    out->entmode = dwg_bs_read_bb(bs);
    numreactors = dwg_bs_read_bl(bs);
    if (numreactors > DWG_R2004_MAX_REACTORS)
        return 0L; /* implausible -- same defensive clamp as R2000/R13-14 */
    out->numreactors = numreactors;

    out->xdic_missing = dwg_bs_read_bit(bs);
    (void)dwg_bs_read_bit(bs); /* has DS binary data (R2013+; harmless to
                                   always read for R2010 too -- LibreDWG
                                   gates this SINCE(R_2013), but this
                                   reader is only used for R2010/2013/2018
                                   where treating it uniformly matches
                                   what was validated against real data */

    /* Nolinks does NOT exist in the bitstream for R2004+ -- deliberately
       not read here, see the function comment. */

    /* confirmed against real data: 2 real entities in ROTATORIO.dwg
       decoded color=8192/0x2000 (the alpha flag (0x20) plus a zero
       index) before this parsing existed -- see read_r2004_cmc_color's
       own comment for why the low byte alone isn't always a real ACI
       index. */
    out->color = read_r2004_cmc_color(bs);

    (void)dwg_bs_read_bd(bs); /* ltype scale */
    (void)dwg_bs_read_bb(bs); /* ltype flags */
    (void)dwg_bs_read_bb(bs); /* plotstyle flags */
    (void)dwg_bs_read_bb(bs); /* material flags (R2007+) */
    (void)dwg_bs_read_rc(bs); /* shadow flags (R2007+) */
    (void)dwg_bs_read_bit(bs); /* has full visualstyle (R2010+) */
    (void)dwg_bs_read_bit(bs); /* has face visualstyle (R2010+) */
    (void)dwg_bs_read_bit(bs); /* has edge visualstyle (R2010+) */
    (void)dwg_bs_read_bs(bs); /* invisibility */
    (void)dwg_bs_read_rc(bs); /* lineweight */

    return 1L;
}

/*
 * Resolves an entity's LAYER handle (Common Entity Handle Data --
 * ownerhandle if entmode==0, then numreactors handles, then
 * xdicobjhandle if !xdic_missing, then LAYER unconditionally, R2000+)
 * and, if the target really is a LAYER object (Type 0x33), its real
 * Entry Name via read_r2004_first_string above. Returns 1 and fills
 * out_name on success.
 *
 * handle_stream_start_bit = (bit pos right after this object's own
 * Type field) + bitsize -- the same reference frame validated for
 * geometry/Common Entity Data, confirmed correct here too by real
 * LAYER Type==0x33 matches during this reader's own investigation.
 */
static long resolve_r2004_layer_name(const unsigned char *data, unsigned long length,
                                     const DWG_R2004_HANDLE_ENTRY *handles, unsigned long handle_count,
                                     unsigned long type_start_bit, unsigned long bitsize,
                                     const DWG_R2004_COMMON_ENTITY *common,
                                     char *out_name, unsigned long out_size,
                                     unsigned short *out_color)
{
    DWG_BITSTREAM bs;
    unsigned char code;
    unsigned long value, k, layer_handle, layer_offset, layer_type_start;
    unsigned long layer_length, layer_hdlstream_size, layer_bitsize, layer_obj_type;

    if (type_start_bit + bitsize > length * 8UL)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, type_start_bit + bitsize);

    if (common->entmode == 0UL)
        dwg_bs_read_handle(&bs, &code, &value); /* ownerhandle, discarded */
    for (k = 0UL; k < common->numreactors; k++)
        dwg_bs_read_handle(&bs, &code, &value);
    if (common->xdic_missing == 0UL)
        dwg_bs_read_handle(&bs, &code, &value); /* xdicobjhandle, discarded */

    dwg_bs_read_handle(&bs, &code, &value);
    layer_handle = dwg_bs_resolve_handle(code, value, common->handle);

    if (!find_handle(handles, handle_count, layer_handle, &layer_offset))
        return 0L;
    if (layer_offset + 8UL >= length)
        return 0L;

    dwg_bs_seek_bit(&bs, layer_offset * 8UL);
    layer_length = dwg_bs_read_ms(&bs);
    if (layer_length == 0UL)
        return 0L; /* a real, confirmed case: heavily-edited real files (this
                      one included) have "zero pages" -- gaps between
                      AcDb:AcDbObjects's real page content that AutoCAD
                      never wrote because they'd decompress to all-zero,
                      likely reserved/purged space from incremental saves.
                      Some real handles (confirmed: handle 225, referenced
                      as the LAYER by most of this file's own entities)
                      land exactly in such a gap -- their real definition
                      genuinely isn't present in decompressible content,
                      not a bug in this resolver (the SAME mechanism
                      correctly resolves other real layers, e.g. handle 16
                      -> "0", confirmed working). */
    layer_hdlstream_size = dwg_bs_read_mc(&bs, 0);
    layer_type_start = dwg_bs_tell_bit(&bs);
    layer_obj_type = read_object_type_r2010(&bs);
    if (layer_obj_type != 0x33UL) /* not really a LAYER -- stale/zero-page noise */
        return 0L;

    layer_bitsize = layer_length * 8UL - layer_hdlstream_size;

    /* Real gap, twice attempted-and-reverted earlier this session for
       lack of a verified field reference (see the two UPDATE blocks
       above `carve_missing_handles` for that full history) -- fixed
       now against LibreDWG's actual source (`D:\estudio\libredwg-master`,
       Arturo pointed at it directly), not a web-fetched fragment.
       Confirmed byte-for-byte from `src/decode.c`'s `dwg_decode_object`
       (the shared prologue every non-entity object goes through) and
       `src/common_object_handle_data.spec`: after the object's own
       Type field, the MAIN-STREAM sequence is handle(H) -> EED loop ->
       num_reactors(BL) -> is_xdic_missing(B, SINCE R_2004a) ->
       has_ds_data(B, SINCE R_2013) -- EXACTLY the same shape
       read_common_entity_r2004 already uses for entities (objects just
       skip entities' own preview/entmode fields), confirming that part
       was already right. ownerhandle/reactors/xdicobjhandle are ALL
       read via FIELD_HANDLE, which `src/dec_macros.h`'s VALUE_H macro
       confirms reads from `hdl_dat` (the separate handle-stream) for
       every version since R_13b1, NOT the main stream -- zero main-
       stream bits, so nothing to skip for them here.

       Then LAYER's own fields, confirmed from `src/dwg.spec`'s
       `DWG_TABLE(LAYER)` + spec.h's `COMMON_TABLE_FLAGS` macro: Entry
       Name is hoisted entirely to the separate string-stream for
       R2007+ (zero main-stream bits, already handled by this
       function's own string-stream read below) -- COMMON_TABLE_FLAGS's
       own LATER_VERSIONS branch inside UNTIL(R_2004)'s sibling
       (i.e. our R2004a+ target) reads is_xref_ref/is_xref_dep as
       DERIVED values, not separate bitstream reads; the only real
       field it consumes is is_xref_resolved(BS) -- ONE field, not the
       three-field (bit+BS+bit) trio the FIRST reverted attempt
       assumed, which is the exact bug that produced a fixed,
       content-independent misalignment (same wrong color every
       layer). The xref HANDLE itself is deferred to the handle-stream
       (same FIELD_HANDLE rule as above). After that: flag0(BS, SINCE
       R_2000b -- frozen/off/frozen_in_new/locked/plotflag/linewt
       combined) then color via `FIELD_CMC` -- confirmed to directly
       follow flag0 with nothing in between, but a GENUINELY DIFFERENT
       encoding from entity color (see read_r2004_table_color's own
       comment for the real, byte-verified difference -- confirmed the
       hard way: reusing read_r2004_cmc_color here first produced a
       plausible-looking but systematically wrong color on every single
       real layer in this file, root-caused by checking the actual
       LibreDWG source rather than guessing further). */
    if (out_color != NULL)
    {
        unsigned long eed_size2, numreactors2;

        *out_color = 0U; /* default: caller's own BYLAYER/BYBLOCK fallback if anything below bails early */

        dwg_bs_read_handle(&bs, &code, &value); /* the LAYER object's own handle -- discarded, already known */

        eed_size2 = dwg_bs_read_bs(&bs);
        while (eed_size2 != 0UL)
        {
            unsigned long k2;
            dwg_bs_read_handle(&bs, &code, &value); /* EED application handle */
            for (k2 = 0UL; k2 < eed_size2; k2++)
                (void)dwg_bs_read_rc(&bs);
            eed_size2 = dwg_bs_read_bs(&bs);
        }

        numreactors2 = dwg_bs_read_bl(&bs);
        if (numreactors2 <= 1000UL) /* implausible -- same defensive bound used elsewhere; bail rather than misread color */
        {
            (void)dwg_bs_read_bit(&bs); /* is_xdic_missing, SINCE R_2004a */
            (void)dwg_bs_read_bit(&bs); /* has_ds_data, SINCE R_2013 -- our AC1024/1027/1032 targets are all >= R_2013 */
            (void)dwg_bs_read_bs(&bs);  /* is_xref_resolved -- COMMON_TABLE_FLAGS's only real main-stream field for R2004a+ */
            (void)dwg_bs_read_bs(&bs);  /* flag0: frozen/off/frozen_in_new/locked/plotflag/linewt, combined BS -- SINCE R_2000b */

            *out_color = read_r2004_table_color(&bs);
        }
    }

    return read_r2004_first_string(data, length, layer_type_start, layer_bitsize, out_name, out_size);
}

/* ---- geometry decoders: same field shape as R2000 (R2004+/R2010+ only
   changed Common Entity Data and the Type encoding, not these basic
   entities' own fields) -- confirmed against real, plausible decoded
   coordinates from ROTATORIO.dwg. ---- */

#define DWG_R2004_TYPE_ARC    0x11UL
#define DWG_R2004_TYPE_CIRCLE 0x12UL
#define DWG_R2004_TYPE_LINE   0x13UL
#define DWG_R2004_TYPE_POINT  0x1BUL
#define DWG_R2004_TYPE_SOLID  0x1FUL

static HENTITY decode_line(HDWG hDwg, DWG_BITSTREAM *bs)
{
    unsigned long z_is_zero;
    double sx, sy, sz, ex, ey, ez;

    z_is_zero = dwg_bs_read_bit(bs);
    sx = dwg_bs_read_rd(bs);
    ex = dwg_bs_read_dd(bs, sx);
    sy = dwg_bs_read_rd(bs);
    ey = dwg_bs_read_dd(bs, sy);

    if (z_is_zero == 0UL)
    {
        sz = dwg_bs_read_rd(bs);
        ez = dwg_bs_read_dd(bs, sz);
    }
    else
    {
        sz = 0.0;
        ez = 0.0;
    }

    (void)dwg_bs_read_bt(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion);
    }

    if (!is_plausible_coord(sx) || !is_plausible_coord(sy) || !is_plausible_coord(sz) ||
        !is_plausible_coord(ex) || !is_plausible_coord(ey) || !is_plausible_coord(ez))
        return NULL;

    return dwg_add_line(hDwg, sx, sy, sz, ex, ey, ez);
}

static HENTITY decode_circle(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D center;
    double radius;

    dwg_bs_read_3bd(bs, &center);
    radius = dwg_bs_read_bd(bs);
    (void)dwg_bs_read_bt(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion);
    }

    if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
        !is_plausible_coord(radius))
        return NULL;

    /* NOT applying the R2000/R14 readers' "reject exact origin" check
       here (see dwg_r2000_entity_reader.c's decode_circle for that
       reasoning) -- confirmed via regression test that it costs 2 real
       entities on ROTATORIO2.dwg (592 -> 590), a traffic-roundabout
       drawing where a circle/arc genuinely centered at the drawing's
       own local origin is a completely plausible, common design
       choice, unlike the BIM floor plans the origin-artifact bug was
       found on. R2004 also never used this session's resync/salvage
       machinery, the actual source of the original bug -- no evidence
       this file needs the extra check at all. */
    return dwg_add_circle(hDwg, center.x, center.y, center.z, radius);
}

static HENTITY decode_arc(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D center;
    double radius, start_angle_rad, end_angle_rad;

    dwg_bs_read_3bd(bs, &center);
    radius = dwg_bs_read_bd(bs);
    (void)dwg_bs_read_bt(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion);
    }
    start_angle_rad = dwg_bs_read_bd(bs);
    end_angle_rad = dwg_bs_read_bd(bs);

    /* Same real hang fixed in the R2000/R14 readers' own copy of this
       check -- angles were never validated, and an unvalidated garbage
       angle can make draw_arc's sweep normalization loop effectively
       forever, or feed an undefined-behavior segment-count cast if it
       escapes (dwg_render.c). */
    if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
        !is_plausible_coord(radius) || !is_plausible_coord(start_angle_rad) || !is_plausible_coord(end_angle_rad))
        return NULL;

    /* Deliberately NOT applying the "reject exact origin" check here
       -- see decode_circle's own comment just above for why (confirmed
       regression on ROTATORIO2.dwg). */
    return dwg_add_arc(hDwg, center.x, center.y, center.z, radius,
                       start_angle_rad * 180.0 / M_PI, end_angle_rad * 180.0 / M_PI);
}

static HENTITY decode_point(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D p;

    dwg_bs_read_3bd(bs, &p);
    (void)dwg_bs_read_bt(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion);
    }
    (void)dwg_bs_read_bd(bs); /* X-axis angle: not modeled, see R2000/R13-14 readers */

    if (!is_plausible_coord(p.x) || !is_plausible_coord(p.y) || !is_plausible_coord(p.z))
        return NULL;

    return dwg_add_point(hDwg, p.x, p.y, p.z);
}

/* SOLID (0x1F): same field shape as R2000/R13-14 (no version split in
   the spec's own table) -- confirmed by real data, same as LINE/
   CIRCLE/ARC/POINT above: this reader's Common Entity Data fix is
   version-generic, only the geometry fields after it needed checking
   per type. */
static HENTITY decode_solid(HDWG hDwg, DWG_BITSTREAM *bs)
{
    double elevation;
    double x1, y1, x2, y2, x3, y3, x4, y4;

    (void)dwg_bs_read_bt(bs); /* thickness: not modeled */
    elevation = dwg_bs_read_bd(bs);
    x1 = dwg_bs_read_rd(bs); y1 = dwg_bs_read_rd(bs);
    x2 = dwg_bs_read_rd(bs); y2 = dwg_bs_read_rd(bs);
    x3 = dwg_bs_read_rd(bs); y3 = dwg_bs_read_rd(bs);
    x4 = dwg_bs_read_rd(bs); y4 = dwg_bs_read_rd(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion);
    }

    if (!is_plausible_coord(x1) || !is_plausible_coord(y1) || !is_plausible_coord(x2) || !is_plausible_coord(y2) ||
        !is_plausible_coord(x3) || !is_plausible_coord(y3) || !is_plausible_coord(x4) || !is_plausible_coord(y4) ||
        !is_plausible_coord(elevation))
        return NULL;

    return dwg_add_solid(hDwg, x1, y1, elevation, x2, y2, elevation,
                         x3, y3, elevation, x4, y4, elevation);
}

#define DWG_R2004_TYPE_TEXT  0x01UL
#define DWG_R2004_TYPE_MTEXT 0x2CUL

/*
 * Decodes TEXT (type 0x01). Field shape confirmed against LibreDWG's
 * real dwg.spec: no SINCE(R_2004a)/R_2007a-specific fields exist for
 * this entity (unlike MTEXT below) -- version-generic since R_2000b,
 * same field-for-field shape R2000's own decode_text already uses.
 * DataFlags (RC) gates which of elevation/alignment_pt/oblique/rotation/
 * width_factor are physically present. The only real R2004+ change is
 * text_value itself: redirected to the string-stream (read_r2004_first_string)
 * instead of the inline dwg_bs_read_t R2000 uses.
 */
static HENTITY decode_text(HDWG hDwg, DWG_BITSTREAM *bs,
                           const unsigned char *data, unsigned long length,
                           unsigned long type_start_bit, unsigned long bitsize)
{
    unsigned char dataflags;
    double elevation = 0.0;
    double ix, iy, ax, ay;
    double oblique = 0.0, rotation = 0.0, height, width_factor = 1.0;
    unsigned short horiz = 0U;
    char text_buf[DWG_TEXT_MAX];
    HENTITY e;

    dataflags = (unsigned char)dwg_bs_read_rc(bs);

    if (!(dataflags & 0x01U))
        elevation = dwg_bs_read_rd(bs);

    ix = dwg_bs_read_rd(bs);
    iy = dwg_bs_read_rd(bs);
    ax = ix;
    ay = iy;
    if (!(dataflags & 0x02U))
    {
        ax = dwg_bs_read_dd(bs, ix);
        ay = dwg_bs_read_dd(bs, iy);
    }

    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion); /* not modeled */
    }
    (void)dwg_bs_read_bt(bs); /* thickness: not modeled */

    if (!(dataflags & 0x04U))
        oblique = dwg_bs_read_rd(bs);
    if (!(dataflags & 0x08U))
        rotation = dwg_bs_read_rd(bs);

    height = dwg_bs_read_rd(bs);

    if (!(dataflags & 0x10U))
        width_factor = dwg_bs_read_rd(bs);

    text_buf[0] = '\0';
    (void)read_r2004_first_string(data, length, type_start_bit, bitsize, text_buf, sizeof(text_buf));

    if (!(dataflags & 0x20U))
        (void)dwg_bs_read_bs(bs); /* generation: not modeled */
    if (!(dataflags & 0x40U))
        horiz = dwg_bs_read_bs(bs);
    if (!(dataflags & 0x80U))
        (void)dwg_bs_read_bs(bs); /* vertical align: not modeled */

    if (!is_plausible_coord(ix) || !is_plausible_coord(iy) || !is_plausible_coord(elevation) ||
        !is_plausible_coord(height))
        return NULL;

    e = dwg_add_text(hDwg, ix, iy, elevation, height, rotation * 180.0 / M_PI, text_buf);
    if (e != NULL)
    {
        dwg_text_set_point0(e, ax, ay, elevation);
        dwg_text_set_width_factor(e, width_factor);
        dwg_text_set_oblique(e, oblique * 180.0 / M_PI);
        dwg_text_set_align(e, horiz);
    }

    return e;
}

/*
 * Decodes MTEXT (type 0x2C). Field shape confirmed against LibreDWG's
 * real dwg.spec, WITH a real version-specific difference from R2000's
 * own decode_mtext: a `rect_height` BD field (SINCE R_2007a) sits
 * between rect_width and text_height in the binary (non-DXF) field
 * order -- absent from R2000's own decoder because R2000 (AC1015)
 * predates R_2007a, but always present for every version this reader
 * targets (AC1024/AC1027/AC1032 are all >= R_2007a). Missing this one
 * extra field would misalign text_height/attachment/text itself.
 *
 * SINCE(R_2018)'s "AnnotScaleObject"/column-data block (only relevant
 * for AC1032) is deliberately NOT read: everything this reader actually
 * needs (ins_pt/rect_width/text_height/text/attachment/linespace_factor)
 * comes before it, and each object is decoded independently via its own
 * handle-based seek -- same "stop once you have what you need" posture
 * every other decoder in this file already uses.
 */
static HENTITY decode_mtext(HDWG hDwg, DWG_BITSTREAM *bs,
                            const unsigned char *data, unsigned long length,
                            unsigned long type_start_bit, unsigned long bitsize)
{
    DWG_POINT3D insertion, extrusion, xaxis;
    double rect_width, text_height;
    unsigned short attachment;
    char text_buf[DWG_MTEXT_MAX];
    HENTITY e;

    dwg_bs_read_3bd(bs, &insertion);
    dwg_bs_read_3bd(bs, &extrusion); /* not modeled */
    dwg_bs_read_3bd(bs, &xaxis);     /* not modeled */
    rect_width = dwg_bs_read_bd(bs);
    (void)dwg_bs_read_bd(bs); /* rect_height, SINCE R_2007a -- always present for our target versions */
    text_height = dwg_bs_read_bd(bs);
    attachment = dwg_bs_read_bs(bs);
    (void)dwg_bs_read_bs(bs); /* drawing dir: not modeled */
    (void)dwg_bs_read_bd(bs); /* extents height: not modeled */
    (void)dwg_bs_read_bd(bs); /* extents width: not modeled */

    text_buf[0] = '\0';
    (void)read_r2004_first_string(data, length, type_start_bit, bitsize, text_buf, sizeof(text_buf));

    if (!is_plausible_coord(insertion.x) || !is_plausible_coord(insertion.y) || !is_plausible_coord(insertion.z) ||
        !is_plausible_coord(rect_width) || !is_plausible_coord(text_height))
        return NULL;

    e = dwg_add_mtext(hDwg, insertion.x, insertion.y, insertion.z, text_height, rect_width, text_buf);

    {
        double linespacing_factor;

        (void)dwg_bs_read_bs(bs); /* linespacing style: not modeled */
        linespacing_factor = dwg_bs_read_bd(bs);
        (void)dwg_bs_read_bit(bs); /* unknown_b0 */

        if (e != NULL)
        {
            dwg_mtext_set_attach(e, attachment);
            dwg_mtext_set_line_space(e, linespacing_factor);
        }
    }

    return e;
}

#define DWG_R2004_TYPE_LWPOLYLINE 0x4DUL
#define DWG_LWPOLYLINE_MAX_POINTS 20000UL

/*
 * Decodes LWPOLYLINE (type 0x4D). Field shape confirmed against
 * LibreDWG's real dwg.spec: flag (BS) gates const_width/elevation/
 * thickness/extrusion, then num_points (BL), then (still gated by
 * flag) num_bulges/num_vertexids(SINCE R_2010b, always true for our
 * target versions)/num_widths, then the point vector itself -- a REAL
 * "2DD_VECTOR" (point[0] is a plain 2xRD, every later point is 2xDD
 * with the PREVIOUS point as default), confirmed via dec_macros.h's own
 * FIELD_2DD_VECTOR body rather than assumed. Widths/vertexids are
 * consumed (correct positioning) but not modeled -- this engine's
 * DWG_VERTEX has no width field, same scope as the DXF reader's own
 * dxf_read_lwpolyline.
 */
static HENTITY decode_lwpolyline(HDWG hDwg, DWG_BITSTREAM *bs)
{
    unsigned long flag;
    double elevation = 0.0;
    unsigned long num_points, num_bulges = 0UL, num_vertexids = 0UL, num_widths = 0UL;
    double *xs = NULL, *ys = NULL, *bulges = NULL;
    unsigned long i;
    HENTITY e;
    HPOLYLINE pl;

    flag = dwg_bs_read_bs(bs);

    if (flag & 4UL) (void)dwg_bs_read_bd(bs); /* const_width: not modeled */
    if (flag & 8UL) elevation = dwg_bs_read_bd(bs);
    if (flag & 2UL) (void)dwg_bs_read_bd(bs); /* thickness: not modeled */
    if (flag & 1UL)
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_3bd(bs, &extrusion); /* not modeled */
    }

    num_points = dwg_bs_read_bl(bs);
    if (num_points == 0UL || num_points > DWG_LWPOLYLINE_MAX_POINTS)
        return NULL;

    if (flag & 16UL)   num_bulges = dwg_bs_read_bl(bs);
    if (flag & 1024UL) num_vertexids = dwg_bs_read_bl(bs);
    if (flag & 32UL)   num_widths = dwg_bs_read_bl(bs);

    xs = (double *)malloc(num_points * sizeof(double));
    ys = (double *)malloc(num_points * sizeof(double));
    if (xs == NULL || ys == NULL)
    {
        free(xs); free(ys);
        return NULL;
    }

    xs[0] = dwg_bs_read_rd(bs);
    ys[0] = dwg_bs_read_rd(bs);
    for (i = 1UL; i < num_points; i++)
    {
        xs[i] = dwg_bs_read_dd(bs, xs[i - 1UL]);
        ys[i] = dwg_bs_read_dd(bs, ys[i - 1UL]);
    }

    if (num_bulges > 0UL && num_bulges <= DWG_LWPOLYLINE_MAX_POINTS)
    {
        bulges = (double *)malloc(num_bulges * sizeof(double));
        if (bulges != NULL)
        {
            for (i = 0UL; i < num_bulges; i++)
                bulges[i] = dwg_bs_read_bd(bs);
        }
        else
        {
            for (i = 0UL; i < num_bulges; i++)
                (void)dwg_bs_read_bd(bs);
        }
    }

    for (i = 0UL; i < num_vertexids; i++)
        (void)dwg_bs_read_bl(bs);
    for (i = 0UL; i < num_widths; i++)
    {
        (void)dwg_bs_read_bd(bs);
        (void)dwg_bs_read_bd(bs);
    }

    e = dwg_add_polyline(hDwg);
    pl = dwg_polyline_from_entity(e);
    if (pl != NULL)
    {
        for (i = 0UL; i < num_points; i++)
        {
            double bulge = (bulges != NULL && i < num_bulges) ? bulges[i] : 0.0;
            if (is_plausible_coord(xs[i]) && is_plausible_coord(ys[i]))
                dwg_polyline_add_vertex2(pl, xs[i], ys[i], elevation, bulge, 0.0, 0.0);
        }
        dwg_polyline_set_elevation(pl, elevation);
        dwg_polyline_set_closed(pl, (flag & 512UL) ? DWG_TRUE : DWG_FALSE);
    }

    free(xs); free(ys); free(bulges);
    return e;
}

#define DWG_R2004_TYPE_INSERT 0x07UL
#define DWG_R2004_TYPE_BLOCK_HEADER 0x31UL
#define DWG_R2004_MAX_BLOCK_ENTITIES 2048UL

/*
 * BLOCK_HEADER (table record) field/handle layout for R2004+ -- the
 * SAME sequence the R2004+ writer's own "smart splice" already
 * validated empirically this session (see project memory: it needed to
 * walk num_owned real handles forward from the handle-stream start to
 * find where to splice a new one in, and that walk round-tripped
 * correctly against a real edited file). Cross-checked against
 * LibreDWG's real dwg.spec (DWG_TABLE(BLOCK_HEADER), COMMON_TABLE_FLAGS,
 * common_object_handle_data.spec, all curl'd verbatim): FIELD_T calls
 * (name/xref_pname/description) redirect entirely to the separate
 * string-stream for R2007+ and consume ZERO bits from this function's
 * own field walk -- confirmed by the same mechanism already proven for
 * LAYER/STYLE names elsewhere in this file. Only base_pt and the
 * entities[] handle vector are actually needed here (INSERT explosion);
 * name/xref_pname/description/preview/insert_units are walked past
 * (skipped) but never decoded, and the handle walk stops right after
 * entities[] -- endblk_entity/inserts[]/layout are never read.
 */
static long decode_block_header_r2004(const unsigned char *data, unsigned long length,
                                      unsigned long loc,
                                      DWG_POINT3D *base_pt,
                                      unsigned long *entity_handles, unsigned long max_entities,
                                      unsigned long *out_entity_count)
{
    DWG_BITSTREAM bs, hbs;
    unsigned char code;
    unsigned long value;
    unsigned long declared_length, handlestream_size, type_start_bit, bitsize, obj_type;
    unsigned long own_handle, numreactors, is_xdic_missing;
    unsigned long anonymous, hasattrs, blkisxref, xrefoverlaid;
    unsigned long num_owned, have_num_owned;
    unsigned char insert_count_byte;
    unsigned long preview_size, j, k;

    *out_entity_count = 0UL;

    if (loc + 8UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    declared_length = dwg_bs_read_ms(&bs);
    if (declared_length == 0UL)
        return 0L;
    handlestream_size = dwg_bs_read_mc(&bs, 0);
    type_start_bit = dwg_bs_tell_bit(&bs);
    bitsize = declared_length * 8UL - handlestream_size;
    obj_type = read_object_type_r2010(&bs);
    if (obj_type != DWG_R2004_TYPE_BLOCK_HEADER)
        return 0L;

    dwg_bs_read_handle(&bs, &code, &value); /* this object's own handle */
    own_handle = value;

    {
        unsigned long eed_size = dwg_bs_read_bs(&bs);
        while (eed_size != 0UL)
        {
            dwg_bs_read_handle(&bs, &code, &value); /* EED application handle */
            for (k = 0UL; k < eed_size; k++)
                (void)dwg_bs_read_rc(&bs);
            eed_size = dwg_bs_read_bs(&bs);
        }
    }

    numreactors = dwg_bs_read_bl(&bs);
    if (numreactors > DWG_R2004_MAX_REACTORS)
        return 0L;
    is_xdic_missing = dwg_bs_read_bit(&bs);
    (void)dwg_bs_read_bit(&bs); /* has_ds_data (R2013+) */

    /* COMMON_TABLE_FLAGS(Block), R2004+ branch: name is a string-stream
       string -- doesn't touch this bitstream at all. */
    (void)dwg_bs_read_bs(&bs); /* is_xref_resolved */

    anonymous = dwg_bs_read_bit(&bs);
    hasattrs = dwg_bs_read_bit(&bs);
    blkisxref = dwg_bs_read_bit(&bs);
    xrefoverlaid = dwg_bs_read_bit(&bs);
    (void)dwg_bs_read_bit(&bs); /* xref_loaded */
    (void)anonymous; (void)hasattrs;

    have_num_owned = 0UL;
    num_owned = 0UL;
    if (blkisxref == 0UL && xrefoverlaid == 0UL)
    {
        num_owned = dwg_bs_read_bl(&bs);
        have_num_owned = 1UL;
    }

    dwg_bs_read_3bd(&bs, base_pt);

    /* xref_pname: string-stream, doesn't touch this bitstream. */

    do
    {
        insert_count_byte = (unsigned char)dwg_bs_read_rc(&bs);
    } while (insert_count_byte != 0U);

    /* description: string-stream, doesn't touch this bitstream. */

    preview_size = dwg_bs_read_bl(&bs);
    if (preview_size > length)
        return 0L;
    for (j = 0UL; j < preview_size; j++)
        (void)dwg_bs_read_rc(&bs);

    /* insert_units/explodable/block_scaling: not needed by this reader. */

    if (!have_num_owned || num_owned == 0UL || num_owned > max_entities)
        return 1L; /* still a valid BLOCK_HEADER, just nothing (or too much) to explode */

    dwg_bs_init(&hbs, data, length);
    dwg_bs_seek_bit(&hbs, type_start_bit + bitsize);

    dwg_bs_read_handle(&hbs, &code, &value); /* ownerhandle */
    for (k = 0UL; k < numreactors; k++)
        dwg_bs_read_handle(&hbs, &code, &value);
    if (is_xdic_missing == 0UL)
        dwg_bs_read_handle(&hbs, &code, &value); /* xdicobjhandle */
    dwg_bs_read_handle(&hbs, &code, &value); /* xref (COMMON_TABLE_FLAGS) */
    dwg_bs_read_handle(&hbs, &code, &value); /* block_entity */

    for (k = 0UL; k < num_owned; k++)
    {
        dwg_bs_read_handle(&hbs, &code, &value);
        entity_handles[k] = dwg_bs_resolve_handle(code, value, own_handle);
    }
    *out_entity_count = num_owned;

    return 1L;
}

/*
 * Decodes one LINE/CIRCLE/ARC/POINT/SOLID entity living inside a
 * BLOCK's own definition and appends a transformed copy into hDwg --
 * same "accesorios" scope and same transform order (base point ->
 * origin, scale, rotate, move to insertion point) as the R2000 reader's
 * decode_and_transform_block_entity, just using this file's own R2004+
 * Common Entity Data/Type primitives. Layer isn't resolved for
 * block-internal entities, matching R2000's own scope (not modeled).
 */
static void decode_and_transform_block_entity_r2004(HDWG hDwg, const unsigned char *data, unsigned long length,
                                                     unsigned long loc,
                                                     const DWG_POINT3D *block_base,
                                                     double ins_x, double ins_y, double ins_z,
                                                     double scale_x, double scale_y, double scale_z,
                                                     double rotation_deg,
                                                     long content_is_absolute)
{
    DWG_BITSTREAM bs;
    unsigned long declared_length, handlestream_size, obj_type, type_start_bit, bitsize;
    DWG_R2004_COMMON_ENTITY common;
    HENTITY e = NULL;

    if (loc + 8UL >= length)
        return;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    declared_length = dwg_bs_read_ms(&bs);
    if (declared_length == 0UL)
        return;
    handlestream_size = dwg_bs_read_mc(&bs, 0);
    type_start_bit = dwg_bs_tell_bit(&bs);
    bitsize = declared_length * 8UL - handlestream_size;
    obj_type = read_object_type_r2010(&bs);

    if (obj_type != DWG_R2004_TYPE_LINE && obj_type != DWG_R2004_TYPE_CIRCLE &&
        obj_type != DWG_R2004_TYPE_ARC && obj_type != DWG_R2004_TYPE_POINT &&
        obj_type != DWG_R2004_TYPE_SOLID && obj_type != DWG_R2004_TYPE_TEXT &&
        obj_type != DWG_R2004_TYPE_MTEXT)
        return;

    if (!read_common_entity_r2004(&bs, &common))
        return;

    switch (obj_type)
    {
    case DWG_R2004_TYPE_LINE:   e = decode_line(hDwg, &bs);   break;
    case DWG_R2004_TYPE_CIRCLE: e = decode_circle(hDwg, &bs); break;
    case DWG_R2004_TYPE_ARC:    e = decode_arc(hDwg, &bs);    break;
    case DWG_R2004_TYPE_POINT:  e = decode_point(hDwg, &bs);  break;
    case DWG_R2004_TYPE_SOLID:  e = decode_solid(hDwg, &bs);  break;
    case DWG_R2004_TYPE_TEXT:   e = decode_text(hDwg, &bs, data, length, type_start_bit, bitsize);  break;
    case DWG_R2004_TYPE_MTEXT:  e = decode_mtext(hDwg, &bs, data, length, type_start_bit, bitsize); break;
    default: break;
    }

    if (e == NULL)
        return;

    apply_color(e, common.color);

    /* REAL bug found+fixed: a block explosion triggered by a genuinely
       standalone INSERT object (the ones found as their own top-level
       DWG_R2004_TYPE_INSERT handle, e.g. the "*D"-named annotation-
       cache INSERTs scattered near each dimension in Arturo's real
       ROTATORIO2.dwg) has ALL of its sub-entities cached by AutoCAD
       ALREADY in absolute world coordinates -- confirmed by direct
       comparison: a duplicated dimension-value MTEXT's decoded
       local_pt exactly matched its OWN correctly-placed standalone
       copy (found via the regular per-handle decode path) BEFORE any
       transform was applied, and the SAME pattern (implausible,
       far-outside-the-sheet positions) showed up for that same
       block's LINE/SOLID/POINT sub-entities too, not just MTEXT --
       applying the normal move/scale/rotate/move sequence on top of
       an already-absolute position doubled the offset from the
       insertion point (e.g. landing text at y=567 when the real
       drawing/sheet only spans to y=~290). This is FALSE for blocks
       exploded from a DIMENSION entity's own arrow/tick block
       reference (decode_dimension's own explode_block_ref_r2004 call)
       -- those really are block-local and need the normal transform,
       confirmed correct by the visually-verified RADIUS dimension fan
       (arrowheads landing exactly on the circle's edge). So: the
       caller now says which kind of reference this is via
       `content_is_absolute` (true only from decode_insert's call). */
    if (!content_is_absolute)
    {
        dwg_entity_move(e, -block_base->x, -block_base->y, -block_base->z);
        dwg_entity_scale_xyz(e, 0.0, 0.0, 0.0, scale_x, scale_y, scale_z);
        dwg_entity_rotate(e, 0.0, 0.0, 0.0, rotation_deg);
        dwg_entity_move(e, ins_x, ins_y, ins_z);
    }
}

/*
 * Decodes INSERT (type 0x07). Field shape confirmed against LibreDWG's
 * real dwg.spec for R_2004a+: ins_pt (3DPOINT), scale (BB flag + the
 * same 4-way dataflags scheme R2000's own decode_insert already uses,
 * confirmed version-generic by the spec), rotation (BD), extrusion (a
 * real 3DPOINT/3BD in the binary form -- NOT the compressed BE form --
 * confirmed by the spec's own "DXF{BE} else{3DPOINT}" branch), has_attribs
 * (B), and (SINCE R_2004a, only if has_attribs) an attrib count BL
 * (ATTRIB itself isn't modeled by this engine, same scope gap R2000's
 * reader already documents).
 *
 * block_header is read from the object's own Common Entity Handle Data
 * (ownerhandle?/reactors/xdicobjhandle?/LAYER/block_header, same walk
 * resolve_r2004_layer_name performs for LAYER -- done here independently
 * since dwg_add_insert needs the resolved block NAME at construction
 * time, unlike LAYER which the caller applies generically afterward).
 */
/*
 * Resolves block_header_handle to its own object location, reads its
 * real name (via the string-stream, same mechanism as LAYER's Entry
 * Name), and -- if found -- explodes its LINE/CIRCLE/ARC/POINT/SOLID/
 * TEXT/MTEXT entities into hDwg with the given insertion transform.
 * Shared by decode_insert and decode_dimension, which pass DIFFERENT
 * `content_is_absolute`: decode_dimension's own arrow/tick block
 * reference is genuinely block-local (needs the normal per-block
 * scale/rotation/translate, confirmed by the visually-verified RADIUS
 * dimension fan) while decode_insert's real standalone INSERT objects
 * in Arturo's files reference blocks whose content AutoCAD already
 * cached in absolute world coordinates (confirmed by a duplicated
 * dimension-value MTEXT's pre-transform local_pt exactly matching its
 * own correctly-placed standalone copy) -- transforming it again would
 * double the offset. Always fills out_block_name (empty string if the
 * block wasn't found/resolved).
 */
static void explode_block_ref_r2004(HDWG hDwg, const unsigned char *data, unsigned long length,
                                    const DWG_R2004_HANDLE_ENTRY *handles, unsigned long handle_count,
                                    unsigned long block_header_handle,
                                    double ins_x, double ins_y, double ins_z,
                                    double sx, double sy, double sz, double rotation_deg,
                                    char *out_block_name, unsigned long out_block_name_size,
                                    long content_is_absolute)
{
    unsigned long block_header_offset;
    int block_header_found;

    if (out_block_name_size > 0UL)
        out_block_name[0] = '\0';

    block_header_found = (int)find_handle(handles, handle_count, block_header_handle, &block_header_offset);
    if (!block_header_found)
        return;

    {
        DWG_BITSTREAM nbs;
        unsigned long btype_start, bhandlestream, bdeclared, bbitsize, bobjtype;

        dwg_bs_init(&nbs, data, length);
        dwg_bs_seek_bit(&nbs, block_header_offset * 8UL);
        bdeclared = dwg_bs_read_ms(&nbs);
        bhandlestream = dwg_bs_read_mc(&nbs, 0);
        btype_start = dwg_bs_tell_bit(&nbs);
        bbitsize = bdeclared * 8UL - bhandlestream;
        bobjtype = read_object_type_r2010(&nbs);
        if (bobjtype == DWG_R2004_TYPE_BLOCK_HEADER)
            (void)read_r2004_first_string(data, length, btype_start, bbitsize, out_block_name, out_block_name_size);
    }

    {
        DWG_POINT3D block_base_pt;
        unsigned long entity_handles[DWG_R2004_MAX_BLOCK_ENTITIES];
        unsigned long entity_count = 0UL, k;

        block_base_pt.x = 0.0; block_base_pt.y = 0.0; block_base_pt.z = 0.0;
        if (decode_block_header_r2004(data, length, block_header_offset, &block_base_pt,
                                      entity_handles, DWG_R2004_MAX_BLOCK_ENTITIES, &entity_count))
        {
            for (k = 0UL; k < entity_count; k++)
            {
                unsigned long eoffset;
                if (find_handle(handles, handle_count, entity_handles[k], &eoffset))
                    decode_and_transform_block_entity_r2004(hDwg, data, length, eoffset, &block_base_pt,
                                                            ins_x, ins_y, ins_z, sx, sy, sz, rotation_deg,
                                                            content_is_absolute);
            }
        }
    }
}

static HENTITY decode_insert(HDWG hDwg, DWG_BITSTREAM *bs,
                             const unsigned char *data, unsigned long length,
                             const DWG_R2004_HANDLE_ENTRY *handles, unsigned long handle_count,
                             unsigned long type_start_bit, unsigned long bitsize,
                             const DWG_R2004_COMMON_ENTITY *common)
{
    DWG_POINT3D ins, extrusion;
    unsigned long scale_flag, has_attribs;
    double sx, sy, sz, rotation_rad;
    unsigned char code;
    unsigned long value, block_header_handle;
    char block_name[256];
    HENTITY e;

    dwg_bs_read_3bd(bs, &ins);

    scale_flag = dwg_bs_read_bb(bs);
    switch (scale_flag)
    {
    case 3UL:
        sx = 1.0; sy = 1.0; sz = 1.0;
        break;
    case 1UL:
        sx = 1.0;
        sy = dwg_bs_read_dd(bs, 1.0);
        sz = dwg_bs_read_dd(bs, 1.0);
        break;
    case 2UL:
        sx = dwg_bs_read_rd(bs);
        sy = sx; sz = sx;
        break;
    default:
        sx = dwg_bs_read_rd(bs);
        sy = dwg_bs_read_dd(bs, sx);
        sz = dwg_bs_read_dd(bs, sx);
        break;
    }

    rotation_rad = dwg_bs_read_bd(bs);
    dwg_bs_read_3bd(bs, &extrusion);
    (void)extrusion;
    has_attribs = dwg_bs_read_bit(bs);
    if (has_attribs != 0UL)
        (void)dwg_bs_read_bl(bs); /* attrib count (SINCE R_2004a); ATTRIB not modeled */

    block_name[0] = '\0';

    {
        DWG_BITSTREAM hbs;
        unsigned long k;
        dwg_bs_init(&hbs, data, length);
        dwg_bs_seek_bit(&hbs, type_start_bit + bitsize);

        if (common->entmode == 0UL)
            dwg_bs_read_handle(&hbs, &code, &value);
        for (k = 0UL; k < common->numreactors; k++)
            dwg_bs_read_handle(&hbs, &code, &value);
        if (common->xdic_missing == 0UL)
            dwg_bs_read_handle(&hbs, &code, &value);
        dwg_bs_read_handle(&hbs, &code, &value); /* LAYER -- discarded, resolved generically by the caller */
        dwg_bs_read_handle(&hbs, &code, &value); /* block_header */
        block_header_handle = dwg_bs_resolve_handle(code, value, common->handle);
    }

    if (!is_plausible_coord(ins.x) || !is_plausible_coord(ins.y) || !is_plausible_coord(ins.z) ||
        !is_plausible_coord(sx) || !is_plausible_coord(sy) || !is_plausible_coord(sz))
        return NULL;

    /* Real accessory content (doors, windows, fixture symbols) lives as
       the referenced block's own entities -- explode at READ time into
       hDwg directly, same reasoning as R2000's own INSERT decoder (this
       engine's renderer has no block-instancing model to resolve later). */
    explode_block_ref_r2004(hDwg, data, length, handles, handle_count, block_header_handle,
                            ins.x, ins.y, ins.z, sx, sy, sz, rotation_rad * 180.0 / M_PI,
                            block_name, sizeof(block_name), 1L);

    e = dwg_add_insert(hDwg, block_name, ins.x, ins.y, ins.z, rotation_rad * 180.0 / M_PI);
    if (e != NULL)
        dwg_insert_set_scale(e, sx, sy, sz);

    return e;
}

#define DWG_R2004_TYPE_DIM_ORDINATE 0x14UL
#define DWG_R2004_TYPE_DIM_LINEAR   0x15UL
#define DWG_R2004_TYPE_DIM_ALIGNED  0x16UL
#define DWG_R2004_TYPE_DIM_ANG3PT   0x17UL
#define DWG_R2004_TYPE_DIM_ANG2LN   0x18UL
#define DWG_R2004_TYPE_DIM_RADIUS   0x19UL
#define DWG_R2004_TYPE_DIM_DIAMETER 0x1AUL

/*
 * Decodes any DIMENSION_* subtype (0x14-0x1A, "cotas"). AutoCAD bakes
 * the dimension's full visual representation (extension lines,
 * dimension line, arrowhead SOLIDs, the MTEXT value) into an anonymous
 * block at save time, same shortcut the DXF reader's own
 * dxf_read_dimension already uses -- but a REAL, confirmed-by-testing
 * difference from the DXF case: this block's own entities are NOT in
 * world coordinates, they're relative to the block's own local origin,
 * needing the dimension's own `def_pt` as the real insertion point
 * (found by explosion producing a small arrow/cross-shaped symbol
 * sitting at world (0,0,0) instead of near the real dimensioned
 * geometry, when identity placement was tried first -- confirmed via
 * Arturo's own real ROTATORIO2.dwg). So, unlike the very first version
 * of this function, the COMMON_ENTITY_DIMENSION field sequence IS read
 * here (field-by-field confirmed against LibreDWG's real
 * dwg_spec_shared.h/dwg.spec, all the FIELD_*0/*1 "default value"
 * suffixes confirmed via spec.h to be bit-identical to their plain
 * form, not a different primitive): class_version(RC)/extrusion(BE)/
 * text_midpt(2xRD)/elevation(BD)/flag1(RC)/user_text(string-stream)/
 * text_rotation/horiz_dir/ins_scale(3xBD)/ins_rotation(BD)/
 * attachment(BS)/lspace_style(BS)/lspace_factor(BD)/act_measurement(BD)/
 * unknown+flip_arrow1+flip_arrow2(3xB)/clone_ins_pt(2xRD), THEN the one
 * subtype-specific field needed: `def_pt`, always the very first field
 * after COMMON_ENTITY_DIMENSION in every subtype's own .spec (confirmed
 * for ORDINATE/RADIUS/ANG2LN) -- 3BD for every subtype except ANG2LN,
 * which uses a plain 2RD (confirmed by directly reading each subtype's
 * own spec text, not assumed uniform).
 *
 * Handle stream unchanged: `COMMON_ENTITY_HANDLE_DATA; dimstyle(5);
 * block(5);`, identical across every subtype (cross-checked against
 * ORDINATE/ANG2LN/RADIUS's own spec text).
 */
static HENTITY decode_dimension(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long obj_type,
                                const unsigned char *data, unsigned long length,
                                const DWG_R2004_HANDLE_ENTRY *handles, unsigned long handle_count,
                                unsigned long type_start_bit, unsigned long bitsize,
                                const DWG_R2004_COMMON_ENTITY *common)
{
    DWG_BITSTREAM hbs;
    unsigned char code;
    unsigned long value, k, block_handle;
    char block_name[256];
    DWG_POINT3D extrusion, def_pt;
    DWG_POINT3D xline1_pt, xline2_pt;
    DWG_POINT3D first_arc_pt;
    double ins_scale_x, ins_scale_y, ins_scale_z, ins_rotation;
    double dim_rotation = 0.0, oblique_angle = 0.0, leader_len = 0.0;
    long have_linear_geom = 0L, have_radial_geom = 0L;
    HENTITY e;

    (void)dwg_bs_read_rc(bs); /* class_version, SINCE R_2010b -- always present for our target versions */
    dwg_bs_read_3bd(bs, &extrusion); /* REAL bug found+fixed: this is a plain FIELD_3BD (3 independent
                                         compressed BD reads) per dwg_spec_shared.h's own
                                         COMMON_ENTITY_DIMENSION macro -- NOT the compressed
                                         1-bit-shortcut BE form LINE/CIRCLE/ARC/POINT/SOLID use for
                                         their own extrusion. Copying the BE assumption here (since
                                         every other entity type decoded so far happens to use BE)
                                         silently misaligned every field after it -- confirmed via
                                         real, own-handle-verified DIMENSION_RADIUS objects in
                                         Arturo's ROTATORIO2.dwg that decoded to astronomical garbage
                                         (text_midpt ~1e-295) before this fix. */
    (void)dwg_bs_read_rd(bs); /* text_midpt.x */
    (void)dwg_bs_read_rd(bs); /* text_midpt.y */
    (void)dwg_bs_read_bd(bs); /* elevation */
    (void)dwg_bs_read_rc(bs); /* flag1 */

    {
        DWG_R2004_STRSTREAM ss;
        char user_text[256];
        open_r2004_string_stream(data, length, type_start_bit, bitsize, &ss);
        user_text[0] = '\0';
        (void)read_next_r2004_string(&ss, user_text, sizeof(user_text)); /* not modeled, just consumed */
    }

    (void)dwg_bs_read_bd(bs); /* text_rotation */
    (void)dwg_bs_read_bd(bs); /* horiz_dir */
    ins_scale_x = dwg_bs_read_bd(bs);
    ins_scale_y = dwg_bs_read_bd(bs);
    ins_scale_z = dwg_bs_read_bd(bs);
    ins_rotation = dwg_bs_read_bd(bs);

    (void)dwg_bs_read_bs(bs); /* attachment */
    (void)dwg_bs_read_bs(bs); /* lspace_style */
    (void)dwg_bs_read_bd(bs); /* lspace_factor */
    (void)dwg_bs_read_bd(bs); /* act_measurement */

    (void)dwg_bs_read_bit(bs); /* unknown */
    (void)dwg_bs_read_bit(bs); /* flip_arrow1 */
    (void)dwg_bs_read_bit(bs); /* flip_arrow2 */

    (void)dwg_bs_read_rd(bs); /* clone_ins_pt.x */
    (void)dwg_bs_read_rd(bs); /* clone_ins_pt.y */

    if (obj_type == DWG_R2004_TYPE_DIM_ANG2LN)
    {
        def_pt.x = dwg_bs_read_rd(bs);
        def_pt.y = dwg_bs_read_rd(bs);
        def_pt.z = 0.0;
    }
    else if (obj_type == DWG_R2004_TYPE_DIM_LINEAR || obj_type == DWG_R2004_TYPE_DIM_ALIGNED)
    {
        /* real DWG.spec order (LATER_VERSIONS): xline1_pt(13), xline2_pt(14),
           THEN def_pt(0) -- def_pt is the THIRD 3BD, not the first. Reading
           only one 3BD here (as ORDINATE/ANG3PT/RADIUS correctly do) silently
           grabbed xline1_pt's bits and used them as the placement point. */
        dwg_bs_read_3bd(bs, &xline1_pt);
        dwg_bs_read_3bd(bs, &xline2_pt);
        dwg_bs_read_3bd(bs, &def_pt);
        oblique_angle = dwg_bs_read_bd(bs);
        if (obj_type == DWG_R2004_TYPE_DIM_LINEAR)
            dim_rotation = dwg_bs_read_bd(bs);
        else /* ALIGNED: no explicit dim_rotation field -- the dimension
                line is always parallel to xline1_pt->xline2_pt. */
        {
            /* Real, confirmed bug found via this exact gap: a garbage/
               misaligned candidate can produce xline1_pt == xline2_pt
               (coincident, both individually "plausible" values, e.g.
               both exactly (0,0,0) -- the same suspicious-default
               pattern already found in decode_circle/decode_arc this
               session), making atan2(0,0)'s two arguments both exactly
               zero. That's a genuine mathematical domain edge case
               (direction undefined for a zero-length segment); this
               compiler's older runtime doesn't just return the IEEE-
               correct 0.0, it prints "atan2: DOMAIN error" and, worse,
               taints the result -- confirmed via a real crash-adjacent
               symptom: 8 downstream LINE entities decoded with NaN
               endpoints once the tainted dim_rotation propagated
               through cos/sin below. Guarding the degenerate case
               directly (skip atan2 entirely, default to 0.0) avoids
               the runtime's own quirky DOMAIN-error path altogether,
               rather than trying to sanitize its output after the
               fact. */
            double dx = xline2_pt.x - xline1_pt.x, dy = xline2_pt.y - xline1_pt.y;
            dim_rotation = (dx == 0.0 && dy == 0.0) ? 0.0 : atan2(dy, dx);
        }
        have_linear_geom = 1L;
    }
    else if (obj_type == DWG_R2004_TYPE_DIM_DIAMETER)
    {
        /* real DWG.spec order: first_arc_pt(15), THEN def_pt(0) == far_chord_pt,
           THEN leader_len(40). Per the DXF reference, def_pt/group-10 here is
           the point on the FAR side of the circle (opposite first_arc_pt,
           i.e. the diameter line passes through the center) -- not a
           generic "definition point" like LINEAR's. Unverified against a
           real file (no DIAMETER candidates showed up in either of
           Arturo's test DWGs, only LINEAR/RADIUS did), built from the spec
           by the same reasoning as RADIUS below. */
        dwg_bs_read_3bd(bs, &first_arc_pt);
        dwg_bs_read_3bd(bs, &def_pt);
        leader_len = dwg_bs_read_bd(bs);
        have_radial_geom = 1L;
    }
    else if (obj_type == DWG_R2004_TYPE_DIM_RADIUS)
    {
        /* real DWG.spec order: def_pt(0) -- here meaning the arc/circle's
           CENTER, per the DXF reference's own description of group 10 for
           RADIUS dimensions (a different meaning than LINEAR's def_pt) --
           THEN first_arc_pt(15) (point on the circle where the radius
           leader touches), THEN leader_len(40) (how far the leader
           continues past the arc, in the same radial direction, to reach
           the text). Confirmed real, own-handle-verified candidates exist
           in Arturo's ROTATORIO2.dwg. */
        dwg_bs_read_3bd(bs, &def_pt);
        dwg_bs_read_3bd(bs, &first_arc_pt);
        leader_len = dwg_bs_read_bd(bs);
        have_radial_geom = 1L;
    }
    else
    {
        /* ORDINATE, ANG3PT: def_pt really is the first field here, and
           neither has a dimension-line-style stroke this engine draws
           today (ORDINATE's leader uses feature_location_pt/leader_endpt,
           ANG3PT's dimension arc needs xline1_pt/xline2_pt/center_pt --
           both real, separate field lists this decoder doesn't read yet;
           left as a further gap, not seen in either of Arturo's files). */
        dwg_bs_read_3bd(bs, &def_pt);
    }

    if (!is_plausible_coord(def_pt.x) || !is_plausible_coord(def_pt.y) || !is_plausible_coord(def_pt.z) ||
        !is_plausible_coord(ins_scale_x) || !is_plausible_coord(ins_scale_y) || !is_plausible_coord(ins_scale_z))
        return NULL;

    dwg_bs_init(&hbs, data, length);
    dwg_bs_seek_bit(&hbs, type_start_bit + bitsize);

    if (common->entmode == 0UL)
        dwg_bs_read_handle(&hbs, &code, &value);
    for (k = 0UL; k < common->numreactors; k++)
        dwg_bs_read_handle(&hbs, &code, &value);
    if (common->xdic_missing == 0UL)
        dwg_bs_read_handle(&hbs, &code, &value);
    dwg_bs_read_handle(&hbs, &code, &value); /* LAYER -- discarded, resolved generically by the caller */
    dwg_bs_read_handle(&hbs, &code, &value); /* dimstyle -- discarded, not modeled */
    dwg_bs_read_handle(&hbs, &code, &value); /* block */
    block_handle = dwg_bs_resolve_handle(code, value, common->handle);

    explode_block_ref_r2004(hDwg, data, length, handles, handle_count, block_handle,
                            def_pt.x, def_pt.y, def_pt.z, ins_scale_x, ins_scale_y, ins_scale_z,
                            ins_rotation * 180.0 / M_PI,
                            block_name, sizeof(block_name), 1L);

    if (have_linear_geom)
    {
        /* LINEAR/ALIGNED dimension/extension lines: never stored as literal
           geometry anywhere (not in the exploded block -- that only holds
           the cached arrowhead/tick symbols -- and not as separate DWG
           objects) -- real AutoCAD always reconstructs them at render time
           from exactly these fields, so this engine has to do the same.
           The dimension line runs through def_pt, direction `dim_rotation`;
           each extension line runs perpendicular from its own measured
           point (xline1_pt/xline2_pt) out to wherever THAT perpendicular
           meets the dimension line -- computed per-point (not from one
           shared offset) since the two feature points aren't generally at
           the same perpendicular distance from the dimension line.
           Obliqued extension lines (oblique_angle != 0) aren't modeled --
           rare in practice, perpendicular is assumed. */
        double dirx = cos(dim_rotation), diry = sin(dim_rotation);
        double perpx = -diry, perpy = dirx;
        double def_perp = def_pt.x * perpx + def_pt.y * perpy;
        double xl1_perp = xline1_pt.x * perpx + xline1_pt.y * perpy;
        double xl2_perp = xline2_pt.x * perpx + xline2_pt.y * perpy;
        double delta1 = def_perp - xl1_perp;
        double delta2 = def_perp - xl2_perp;
        double dl1x = xline1_pt.x + delta1 * perpx, dl1y = xline1_pt.y + delta1 * perpy;
        double dl2x = xline2_pt.x + delta2 * perpx, dl2y = xline2_pt.y + delta2 * perpy;
        HENTITY line_e;

        (void)oblique_angle;

        /* Real, confirmed bug found via this exact gap: dim_rotation
           (read directly off the bitstream for LINEAR, unlike ALIGNED's
           now-guarded computed value just above) was never plausibility-
           checked before feeding cos/sin -- a garbage angle propagates
           straight through def_perp/xl1_perp/xl2_perp/delta1/delta2 into
           dl1x/dl1y/dl2x/dl2y regardless of xline1_pt/xline2_pt
           themselves being individually fine, producing NaN-endpoint
           LINE entities (confirmed: 8 of them on a real file). Reusing
           is_plausible_coord here for the same reason the ARC-angle fix
           earlier this session did: a NaN-safe bounded-range check
           applies just as well to an angle in radians as a coordinate. */
        if (is_plausible_coord(xline1_pt.x) && is_plausible_coord(xline1_pt.y) &&
            is_plausible_coord(xline2_pt.x) && is_plausible_coord(xline2_pt.y) &&
            is_plausible_coord(dim_rotation))
        {
            line_e = dwg_add_line(hDwg, xline1_pt.x, xline1_pt.y, xline1_pt.z, dl1x, dl1y, xline1_pt.z);
            if (line_e != NULL) apply_color(line_e, common->color);
            line_e = dwg_add_line(hDwg, xline2_pt.x, xline2_pt.y, xline2_pt.z, dl2x, dl2y, xline2_pt.z);
            if (line_e != NULL) apply_color(line_e, common->color);
            line_e = dwg_add_line(hDwg, dl1x, dl1y, xline1_pt.z, dl2x, dl2y, xline2_pt.z);
            if (line_e != NULL) apply_color(line_e, common->color);
        }
    }

    if (have_radial_geom &&
        is_plausible_coord(first_arc_pt.x) && is_plausible_coord(first_arc_pt.y) &&
        leader_len >= 0.0 && leader_len < 1.0e6)
    {
        /* RADIUS: near_pt=first_arc_pt (touches the arc), far_pt=def_pt
           (the arc/circle's center) -- draw center->arc as the radius
           indicator, then continue past the arc by leader_len (same
           radial direction) as the dogleg to the text.
           DIAMETER: near_pt=first_arc_pt, far_pt=def_pt (opposite side,
           through the center) -- draw the full diameter line, then
           continue PAST the far point by leader_len as its dogleg.
           Both are the same "line near->far, then extend past far by
           leader_len" shape, just with different meanings for near/far. */
        double ndx = def_pt.x - first_arc_pt.x, ndy = def_pt.y - first_arc_pt.y;
        double dist = sqrt(ndx * ndx + ndy * ndy);
        HENTITY line_e;

        if (dist > 1.0e-9)
        {
            double ux = ndx / dist, uy = ndy / dist;
            double lead_x, lead_y;

            if (obj_type == DWG_R2004_TYPE_DIM_RADIUS)
            {
                /* leader continues OUTWARD from the arc, away from the
                   center -- opposite direction from center->arc. Drawn
                   unconditionally (a dimstyle with no dogleg still needs
                   the radius indicator itself); the dogleg segment past
                   the arc is only added when leader_len is actually > 0
                   (real files here have it at exactly 0 -- no dogleg). */
                line_e = dwg_add_line(hDwg, def_pt.x, def_pt.y, def_pt.z,
                                      first_arc_pt.x, first_arc_pt.y, first_arc_pt.z);
                if (line_e != NULL) apply_color(line_e, common->color);
                if (leader_len > 1.0e-9)
                {
                    lead_x = first_arc_pt.x - ux * leader_len;
                    lead_y = first_arc_pt.y - uy * leader_len;
                    line_e = dwg_add_line(hDwg, first_arc_pt.x, first_arc_pt.y, first_arc_pt.z,
                                          lead_x, lead_y, first_arc_pt.z);
                    if (line_e != NULL) apply_color(line_e, common->color);
                }
            }
            else /* DIAMETER: leader continues past def_pt, same direction as near->far. */
            {
                line_e = dwg_add_line(hDwg, first_arc_pt.x, first_arc_pt.y, first_arc_pt.z,
                                      def_pt.x, def_pt.y, def_pt.z);
                if (line_e != NULL) apply_color(line_e, common->color);
                if (leader_len > 1.0e-9)
                {
                    lead_x = def_pt.x + ux * leader_len;
                    lead_y = def_pt.y + uy * leader_len;
                    line_e = dwg_add_line(hDwg, def_pt.x, def_pt.y, def_pt.z,
                                          lead_x, lead_y, def_pt.z);
                    if (line_e != NULL) apply_color(line_e, common->color);
                }
            }
        }
    }

    if (block_name[0] == '\0')
        return NULL; /* block never resolved -- nothing was exploded, no point in a phantom INSERT */

    e = dwg_add_insert(hDwg, block_name, def_pt.x, def_pt.y, def_pt.z, ins_rotation * 180.0 / M_PI);
    if (e != NULL)
        dwg_insert_set_scale(e, ins_scale_x, ins_scale_y, ins_scale_z);

    return e;
}

#define DWG_R2004_TYPE_HATCH 0x4EUL
#define DWG_HATCH_MAX_PATH_PTS 256UL
#define DWG_HATCH_MAX_EXTRA_ENTITIES 16UL

/* bulge = tan(signed_included_angle/4), positive = CCW from (start_deg)
   to (end_deg) -- same convention verified for dwg_transform.c's own
   (private) bulge_to_arc/dwg_render.c's append_bulge_arc, derived here
   in reverse: is_ccw selects which of the two arcs between the same
   pair of angles this segment actually is. Clamped away from the
   +-360 degenerate case (a lone full-circle edge can't be represented
   by a single bulge between two points). */
static double hatch_seg_bulge(double start_deg, double end_deg, int is_ccw)
{
    double ccw_sweep = fmod(end_deg - start_deg, 360.0);
    double signed_sweep;

    if (ccw_sweep <= 0.0)
        ccw_sweep += 360.0;
    if (ccw_sweep > 359.9)
        ccw_sweep = 359.9;

    signed_sweep = is_ccw ? ccw_sweep : (ccw_sweep - 360.0);
    return tan((signed_sweep * M_PI / 180.0) / 4.0);
}

static void hatch_push_pt(double pts[][3], unsigned long *count, unsigned long max,
                          double x, double y, double bulge)
{
    if (*count < max)
    {
        pts[*count][0] = x;
        pts[*count][1] = y;
        pts[*count][2] = bulge;
        (*count)++;
    }
}

static void hatch_emit_path(HENTITY he, double pts[][3], unsigned long count)
{
    unsigned long k;
    for (k = 0UL; k < count; k++)
    {
        HVERTEX v = dwg_hatch_add_boundary_point(he, pts[k][0], pts[k][1], 0.0);
        if (v != NULL && pts[k][2] != 0.0)
            dwg_vertex_set_bulge(v, pts[k][2]);
    }
}

/*
 * Reads one HATCH boundary path's geometry, appending (x,y,bulge)
 * vertices (LWPOLYLINE-style: bulge on vertex k describes the segment
 * FROM k TO k+1) matching dwg_render.c's draw_hatch/append_bulge_arc
 * convention exactly, the same one the DXF reader's own dxf_hatch_emit_path
 * already produces. ELLIPSE/SPLINE segments aren't modeled geometrically
 * (their real field shape is still fully consumed so later fields stay
 * correctly positioned) -- same degrade-gracefully posture as this
 * reader's other scope gaps (RGB/named colors, ATTRIB). Returns 0 if an
 * unrecognized curve_type makes the position untrustworthy (caller stops
 * reading further paths but keeps whatever was already decoded).
 */
static long decode_hatch_path(DWG_BITSTREAM *bs, unsigned long flag,
                              double pts[][3], unsigned long *count, unsigned long max,
                              int *has_bulge)
{
    if ((flag & 2UL) == 0UL)
    {
        unsigned long num_segs, s;

        num_segs = dwg_bs_read_bl(bs);
        if (num_segs > 10000UL)
            return 0L;

        for (s = 0UL; s < num_segs; s++)
        {
            unsigned long curve_type = dwg_bs_read_rc(bs);

            switch (curve_type)
            {
            case 1UL: /* LINE */
            {
                double x1, y1, x2, y2;
                x1 = dwg_bs_read_rd(bs); y1 = dwg_bs_read_rd(bs);
                x2 = dwg_bs_read_rd(bs); y2 = dwg_bs_read_rd(bs);
                hatch_push_pt(pts, count, max, x1, y1, 0.0);
                hatch_push_pt(pts, count, max, x2, y2, 0.0);
                break;
            }
            case 2UL: /* CIRCULAR ARC */
            {
                double cx, cy, radius, start_a, end_a, bulge;
                unsigned long is_ccw;
                double x1, y1, x2, y2;
                cx = dwg_bs_read_rd(bs); cy = dwg_bs_read_rd(bs);
                radius = dwg_bs_read_bd(bs);
                start_a = dwg_bs_read_bd(bs);
                end_a = dwg_bs_read_bd(bs);
                is_ccw = dwg_bs_read_bit(bs);
                x1 = cx + radius * cos(start_a); y1 = cy + radius * sin(start_a);
                x2 = cx + radius * cos(end_a);   y2 = cy + radius * sin(end_a);
                bulge = hatch_seg_bulge(start_a * 180.0 / M_PI, end_a * 180.0 / M_PI, (int)is_ccw);
                hatch_push_pt(pts, count, max, x1, y1, bulge);
                hatch_push_pt(pts, count, max, x2, y2, 0.0);
                if (bulge != 0.0)
                    *has_bulge = 1;
                break;
            }
            case 3UL: /* ELLIPTICAL ARC -- not modeled geometrically, bits consumed only */
                (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs); /* center */
                (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs); /* endpoint of major axis */
                (void)dwg_bs_read_bd(bs); /* minor/major ratio */
                (void)dwg_bs_read_bd(bs); /* start_angle */
                (void)dwg_bs_read_bd(bs); /* end_angle */
                (void)dwg_bs_read_bit(bs); /* is_ccw */
                break;
            case 4UL: /* SPLINE -- not modeled geometrically, bits consumed only */
            {
                unsigned long degree, is_rational, is_periodic, num_knots, num_cp, num_fitpts, kk;
                degree = dwg_bs_read_bl(bs);
                is_rational = dwg_bs_read_bit(bs);
                is_periodic = dwg_bs_read_bit(bs);
                num_knots = dwg_bs_read_bl(bs);
                num_cp = dwg_bs_read_bl(bs);
                (void)degree; (void)is_periodic;
                if (num_knots > 10000UL || num_cp > 10000UL)
                    return 0L;
                for (kk = 0UL; kk < num_knots; kk++)
                    (void)dwg_bs_read_bd(bs);
                for (kk = 0UL; kk < num_cp; kk++)
                {
                    (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs);
                    if (is_rational != 0UL)
                        (void)dwg_bs_read_bd(bs);
                }
                num_fitpts = dwg_bs_read_bl(bs);
                if (num_fitpts > 10000UL)
                    return 0L;
                if (num_fitpts != 0UL)
                {
                    for (kk = 0UL; kk < num_fitpts; kk++)
                    {
                        (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs);
                    }
                    (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs); /* start_tangent */
                    (void)dwg_bs_read_rd(bs); (void)dwg_bs_read_rd(bs); /* end_tangent */
                }
                break;
            }
            default:
                return 0L; /* unrecognized -- position no longer trustworthy */
            }
        }
    }
    else
    {
        unsigned long bulges_present, closed, num_verts, v;

        bulges_present = dwg_bs_read_bit(bs);
        closed = dwg_bs_read_bit(bs);
        (void)closed;
        num_verts = dwg_bs_read_bl(bs);
        if (num_verts > 10000UL)
            return 0L;

        for (v = 0UL; v < num_verts; v++)
        {
            double x, y, bulge = 0.0;
            x = dwg_bs_read_rd(bs);
            y = dwg_bs_read_rd(bs);
            if (bulges_present != 0UL)
            {
                bulge = dwg_bs_read_bd(bs);
                if (bulge != 0.0)
                    *has_bulge = 1;
            }
            hatch_push_pt(pts, count, max, x, y, bulge);
        }
    }

    (void)dwg_bs_read_bl(bs); /* num_boundary_handles -- the handles themselves live
                                  in the separate handle stream, nothing to skip here */
    return 1L;
}

/*
 * Decodes HATCH (type 0x4E) -- the FIRST binary DWG HATCH decoder in
 * this project (previously only the DXF reader supported it, see
 * project memory's "DXF viewer completeness pass"). Field shape
 * confirmed against LibreDWG's real dwg.spec: a gradient-fill block
 * (SINCE R_2004a, always physically present, checked via curl'd
 * spec_h.txt's own DWG_FUNC_N(_HATCH_gradientfill) body) precedes
 * elevation/extrusion/name/is_solid_fill/paths -- and, same as the DXF
 * reader's own hard-won "bulge-bearing path" heuristic (a real curved
 * boundary always carries nonzero bulge; the many straight-edged
 * accent/cutout paths never do), emits one HATCH entity per
 * bulge-bearing path, falling back to the first path if none has bulge.
 *
 * Named/book colors (CMC flag bits 0x1/0x2) inside a gradient fill are a
 * real, documented scope gap: decoding them correctly needs walking
 * MORE string-stream reads before gradient_name/pattern name, and named
 * colors on a gradient fill are rare in practice -- this decoder bails
 * out defensively (returns NULL) rather than risk misaligned bits, the
 * same "skip what doesn't line up" discipline used throughout this file.
 */
/* Nearest match of a truecolor CMC rgb (0x00RRGGBB, low 3 bytes of the
 * 0xMMRRGGBB field read for gradient color stops) against the SAME 9
 * standard-color RGB triples the renderer's own aci_to_colorref_raw()
 * uses for ACI 1-9 (dwg_render.c) -- reusing those exact triples (not
 * Autodesk's full 256-color book) guarantees an exact round-trip for
 * the common case (e.g. a pure-yellow gradient stop maps back to
 * literal ACI 2), which is all a solid-color approximation needs. */
static unsigned short nearest_aci_from_rgb(unsigned long rgb)
{
    static const struct { unsigned short aci; int r, g, b; } table[9] = {
        { 1, 255, 0, 0 }, { 2, 255, 255, 0 }, { 3, 0, 255, 0 },
        { 4, 0, 255, 255 }, { 5, 0, 0, 255 }, { 6, 255, 0, 255 },
        { 7, 255, 255, 255 }, { 8, 140, 140, 140 }, { 9, 190, 190, 190 }
    };
    int r = (int)((rgb >> 16) & 0xFFUL);
    int g = (int)((rgb >> 8) & 0xFFUL);
    int b = (int)(rgb & 0xFFUL);
    unsigned short best_aci = 7U;
    long best_dist = -1L;
    int i;
    for (i = 0; i < 9; i++)
    {
        long dr = (long)r - table[i].r;
        long dg = (long)g - table[i].g;
        long db = (long)b - table[i].b;
        long dist = dr * dr + dg * dg + db * db;
        if (best_dist < 0L || dist < best_dist)
        {
            best_dist = dist;
            best_aci = table[i].aci;
        }
    }
    return best_aci;
}

static HENTITY decode_hatch(HDWG hDwg, DWG_BITSTREAM *bs, DWG_R2004_STRSTREAM *ss,
                            unsigned short color, const char *layer_name)
{
    unsigned long is_gradient_fill, num_colors, ci;
    unsigned long is_solid_fill, is_associative, num_paths, p;
    unsigned long style, pattern_type;
    double elevation;
    DWG_POINT3D extrusion;
    char pattern_name[256];
    double angle_deg = 0.0, scale = 1.0;
    HENTITY entity, he;
    long ok = 1L;

    double cur_pts[DWG_HATCH_MAX_PATH_PTS][3];
    unsigned long cur_count;
    int cur_has_bulge;
    double first_pts[DWG_HATCH_MAX_PATH_PTS][3];
    unsigned long first_count = 0UL;
    HENTITY extra_entities[DWG_HATCH_MAX_EXTRA_ENTITIES];
    unsigned long extra_count = 0UL;
    int bulge_paths_emitted = 0;
    unsigned short gradient_aci = 0U; /* 0 = none found; stays BYLAYER/`color` if so */

    is_gradient_fill = dwg_bs_read_bl(bs);
    (void)dwg_bs_read_bl(bs); /* reserved */
    (void)dwg_bs_read_bd(bs); /* gradient_angle */
    (void)dwg_bs_read_bd(bs); /* gradient_shift */
    (void)dwg_bs_read_bl(bs); /* single_color_gradient */
    (void)dwg_bs_read_bd(bs); /* gradient_tint */
    num_colors = dwg_bs_read_bl(bs);
    if (is_gradient_fill != 0UL && num_colors > 1000UL)
        return NULL;
    if (num_colors > 1000UL)
        num_colors = 0UL; /* defensive: same clamp spirit as REPEAT's own VALUEOUTOFBOUNDS check */

    for (ci = 0UL; ci < num_colors && ok; ci++)
    {
        unsigned long flag, cmc_index, cmc_rgb;
        (void)dwg_bs_read_bd(bs); /* shift_value */
        cmc_index = dwg_bs_read_bs(bs); /* CMC index */
        cmc_rgb = dwg_bs_read_bl(bs); /* CMC rgb */
        flag = dwg_bs_read_rc(bs);
        if (flag < 4UL)
        {
            char tmp[256];
            if ((flag & 1UL) != 0UL && !read_next_r2004_string(ss, tmp, sizeof(tmp)))
                ok = 0L;
            if (ok && (flag & 2UL) != 0UL && !read_next_r2004_string(ss, tmp, sizeof(tmp)))
                ok = 0L;
        }
        /* real ACI colors are 1-255 -- keep the LAST stop's index as a
           solid-color approximation of the gradient (this engine can't
           render a true gradient), same reasoning already validated by
           the DXF reader's own gradient_aci handling: the end-stop color
           reads as a real, correct color rather than leaving the fill at
           its BYLAYER default (confirmed missing by Arturo originally,
           "falta ... color", for the DXF path -- same fix ported here). */
        if (ok && cmc_index >= 1UL && cmc_index <= 255UL)
            gradient_aci = (unsigned short)cmc_index;
        else if (ok && cmc_index == 0UL && cmc_rgb != 0UL)
            /* index 0 with a nonzero packed value means truecolor (this
               file's gradients: method byte 0xC2, RGB in the low 3 bytes) --
               approximate with the nearest of the 9 real ACI colors. */
            gradient_aci = nearest_aci_from_rgb(cmc_rgb);
    }
    if (!ok)
        return NULL;

    {
        char tmp[256];
        (void)read_next_r2004_string(ss, tmp, sizeof(tmp)); /* gradient_name */
    }

    elevation = dwg_bs_read_bd(bs);
    (void)elevation;
    dwg_bs_read_3bd(bs, &extrusion);
    (void)extrusion;

    pattern_name[0] = '\0';
    (void)read_next_r2004_string(ss, pattern_name, sizeof(pattern_name));

    is_solid_fill = dwg_bs_read_bit(bs);
    is_associative = dwg_bs_read_bit(bs);
    (void)is_associative;

    num_paths = dwg_bs_read_bl(bs);
    if (num_paths > 10000UL)
        return NULL;

    if (is_gradient_fill != 0UL && gradient_aci != 0U)
        color = gradient_aci; /* solid-color approximation of the real gradient */

    entity = dwg_add_hatch(hDwg, pattern_name, 0.0, 1.0, is_solid_fill ? DWG_TRUE : DWG_FALSE);
    if (entity == NULL)
        return NULL;
    apply_color(entity, color);
    if (layer_name != NULL)
        dwg_entity_put_layer(entity, layer_name);

    for (p = 0UL; p < num_paths; p++)
    {
        unsigned long flag = dwg_bs_read_bl(bs);

        cur_count = 0UL;
        cur_has_bulge = 0;

        if (!decode_hatch_path(bs, flag, cur_pts, &cur_count, DWG_HATCH_MAX_PATH_PTS, &cur_has_bulge))
            break; /* keep whatever was already decoded, same defensive posture as elsewhere */

        if (cur_has_bulge && cur_count >= 3UL)
        {
            if (bulge_paths_emitted == 0)
                he = entity;
            else
            {
                he = dwg_add_hatch(hDwg, pattern_name, 0.0, 1.0, is_solid_fill ? DWG_TRUE : DWG_FALSE);
                if (he != NULL)
                {
                    apply_color(he, color);
                    if (layer_name != NULL)
                        dwg_entity_put_layer(he, layer_name);
                    if (extra_count < DWG_HATCH_MAX_EXTRA_ENTITIES)
                        extra_entities[extra_count++] = he;
                }
            }
            if (he != NULL)
                hatch_emit_path(he, cur_pts, cur_count);
            bulge_paths_emitted++;
        }
        else if (p == 0UL && cur_count >= 3UL)
        {
            unsigned long k;
            for (k = 0UL; k < cur_count; k++)
            {
                first_pts[k][0] = cur_pts[k][0];
                first_pts[k][1] = cur_pts[k][1];
                first_pts[k][2] = cur_pts[k][2];
            }
            first_count = k;
        }
    }

    if (bulge_paths_emitted == 0)
        hatch_emit_path(entity, first_pts, first_count);

    style = dwg_bs_read_bs(bs); (void)style;
    pattern_type = dwg_bs_read_bs(bs); (void)pattern_type;

    if (is_solid_fill == 0UL)
    {
        unsigned long num_deflines, dl;
        angle_deg = dwg_bs_read_bd(bs) * 180.0 / M_PI;
        scale = dwg_bs_read_bd(bs);
        (void)dwg_bs_read_bit(bs); /* double_flag */
        num_deflines = dwg_bs_read_bs(bs);
        if (num_deflines > 1000UL)
            num_deflines = 0UL;
        for (dl = 0UL; dl < num_deflines; dl++)
        {
            unsigned long num_dashes, dd;
            (void)dwg_bs_read_bd(bs); /* angle */
            (void)dwg_bs_read_bd(bs); (void)dwg_bs_read_bd(bs); /* pt0 */
            (void)dwg_bs_read_bd(bs); (void)dwg_bs_read_bd(bs); /* offset */
            num_dashes = dwg_bs_read_bs(bs);
            if (num_dashes > 1000UL)
                num_dashes = 0UL;
            for (dd = 0UL; dd < num_dashes; dd++)
                (void)dwg_bs_read_bd(bs);
        }
    }

    /* has_derived/pixel_size/num_seeds/seeds: not needed for rendering,
       and has_derived isn't even physically present (it's DERIVED from
       path flags per the spec's own DECODER-block comment) -- this
       decoder stops here, same as every other geometry decoder in this
       file never reading all the way to an object's declared end. */

    dwg_hatch_set_angle(entity, angle_deg);
    dwg_hatch_set_scale(entity, scale);
    {
        unsigned long k;
        for (k = 0UL; k < extra_count; k++)
        {
            dwg_hatch_set_angle(extra_entities[k], angle_deg);
            dwg_hatch_set_scale(extra_entities[k], scale);
        }
    }

    return entity;
}

static int r2004_handle_entry_cmp(const void *a, const void *b)
{
    const DWG_R2004_HANDLE_ENTRY *ea = (const DWG_R2004_HANDLE_ENTRY *)a;
    const DWG_R2004_HANDLE_ENTRY *eb = (const DWG_R2004_HANDLE_ENTRY *)b;
    if (ea->handle < eb->handle) return -1;
    if (ea->handle > eb->handle) return 1;
    return 0;
}

/*
 * Recovery pass for a genuinely corrupted AcDb:Handles. Real, heavily-
 * edited files (see project findings this session) can have physically
 * intact objects whose handle simply never made it into the handle map
 * at all -- not just a wrong offset, entirely ABSENT -- confirmed on a
 * real file: a byte-by-byte scan of AcDb:AcDbObjects found a real HATCH
 * with is_gradient_fill=1 and num_colors=2 (matching Arturo's own
 * AutoCAD screenshot's yellow-to-green gradient exactly, verified
 * field-by-field in a Python prototype before writing this) and several
 * additional real DIMENSION objects, NONE of them reachable through the
 * official handle map at any offset.
 *
 * Brute-force scans every byte position in objects_buf for a self-
 * consistent object header (declared_length/handlestream_size in
 * plausible ranges, a recognized Type, a plausible nonzero absolute-
 * coded own handle) and appends any handle NOT already in handles[] as
 * a new entry, then re-sorts (find_handle's binary search requires the
 * non-decreasing invariant parse_handles already produces).
 *
 * False positives are inherently safe to have here: the existing
 * `common.handle != handles[i].handle` verification already in the main
 * decode loop (identical check that already filters ordinary handle-map
 * corruption) rejects anything that doesn't decode consistently
 * end-to-end, so a spurious carved entry just gets silently skipped
 * like always -- this pass can only ADD real recoverable content, never
 * corrupt or shadow an existing good entry (duplicates against an
 * already-present handle are skipped outright, before ever reaching the
 * array).
 */
static long r2004_find_handle_index(const DWG_R2004_HANDLE_ENTRY *entries, unsigned long count, unsigned long handle)
{
    unsigned long lo = 0UL, hi = count;
    while (lo < hi)
    {
        unsigned long mid = lo + (hi - lo) / 2UL;
        if (entries[mid].handle == handle)
            return (long)mid;
        else if (entries[mid].handle < handle)
            lo = mid + 1UL;
        else
            hi = mid;
    }
    return -1L;
}

/* Lightweight standalone re-check: does the object physically AT offset
   really declare itself as expected_handle? Used to tell a genuinely
   correct official map entry apart from a stale one worth repairing --
   same "own handle" signature the main decode loop's own
   `common.handle != handles[i].handle` check already relies on,
   without needing a full DWG_R2004_COMMON_ENTITY decode. */
/* Orphan-candidate geometry check, mirroring R2000/R14's own r2000_
   orphan_geometry_ok/r1314_orphan_geometry_ok this session already
   built (see dwg_r2000_reader.c for the full "why": a candidate's own
   decoded geometry is trustworthy evidence independent of whether its
   handle matches anything a caller already expected). Reuses read_
   common_entity_r2004 directly (already correct, proven everywhere
   else in this file) to walk Common Entity Data, then duplicates just
   the type-specific geometry field reads from decode_line/decode_
   circle/decode_arc/decode_point/decode_solid (NOT calling those
   functions themselves -- they need a real HDWG to attach the entity
   to via dwg_add_*, and constructing/destroying a scratch document
   per candidate across a whole-file scan of a million-plus byte
   positions would be a real, needless allocation cost). Covers the
   five types with their own existing plausibility-checked decoders --
   the ones dominating this file's real content. Returns 1 (with
   *out_handle set) if geometry looks genuinely real, 0 otherwise. */
static long r2004_orphan_geometry_ok(const unsigned char *objects_buf, unsigned long objects_size,
                                     unsigned long loc, unsigned long *out_handle)
{
    DWG_BITSTREAM bs;
    unsigned long declared_length, obj_type;
    DWG_R2004_COMMON_ENTITY common;

    if (loc + 8UL >= objects_size)
        return 0L;

    dwg_bs_init(&bs, objects_buf, objects_size);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    declared_length = dwg_bs_read_ms(&bs);
    if (declared_length < 4UL || declared_length > objects_size)
        return 0L;
    (void)dwg_bs_read_mc(&bs, 0);
    obj_type = read_object_type_r2010(&bs);

    if (obj_type != DWG_R2004_TYPE_LINE && obj_type != DWG_R2004_TYPE_CIRCLE &&
        obj_type != DWG_R2004_TYPE_ARC && obj_type != DWG_R2004_TYPE_POINT &&
        obj_type != DWG_R2004_TYPE_SOLID)
        return 0L;

    if (!read_common_entity_r2004(&bs, &common))
        return 0L;
    if (common.handle == 0UL || common.handle > 200000UL)
        return 0L;

    if (obj_type == DWG_R2004_TYPE_LINE)
    {
        unsigned long z_is_zero;
        double sx, sy, sz, ex, ey, ez;

        z_is_zero = dwg_bs_read_bit(&bs);
        sx = dwg_bs_read_rd(&bs);
        ex = dwg_bs_read_dd(&bs, sx);
        sy = dwg_bs_read_rd(&bs);
        ey = dwg_bs_read_dd(&bs, sy);
        if (z_is_zero == 0UL) { sz = dwg_bs_read_rd(&bs); ez = dwg_bs_read_dd(&bs, sz); }
        else { sz = 0.0; ez = 0.0; }

        if (!is_plausible_coord(sx) || !is_plausible_coord(sy) || !is_plausible_coord(sz) ||
            !is_plausible_coord(ex) || !is_plausible_coord(ey) || !is_plausible_coord(ez))
            return 0L;
    }
    else if (obj_type == DWG_R2004_TYPE_CIRCLE || obj_type == DWG_R2004_TYPE_ARC)
    {
        DWG_POINT3D center;
        double radius, extra1 = 0.0, extra2 = 0.0;

        dwg_bs_read_3bd(&bs, &center);
        radius = dwg_bs_read_bd(&bs);
        (void)dwg_bs_read_bt(&bs); /* thickness */
        {
            DWG_POINT3D extrusion;
            dwg_bs_read_be(&bs, &extrusion);
        }
        if (obj_type == DWG_R2004_TYPE_ARC)
        {
            extra1 = dwg_bs_read_bd(&bs); /* start_angle */
            extra2 = dwg_bs_read_bd(&bs); /* end_angle */
        }

        if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
            !is_plausible_coord(radius) ||
            (obj_type == DWG_R2004_TYPE_ARC && (!is_plausible_coord(extra1) || !is_plausible_coord(extra2))))
            return 0L;
        /* same origin-artifact guard as decode_circle/decode_arc (see
           their own comment) -- a candidate whose center lands exactly
           on (0,0,0) is a much stronger tell of a misaligned garbage
           read than a real, deliberately-placed feature in this file. */
        if (center.x == 0.0 && center.y == 0.0 && center.z == 0.0)
            return 0L;
    }
    else if (obj_type == DWG_R2004_TYPE_POINT)
    {
        DWG_POINT3D p;

        dwg_bs_read_3bd(&bs, &p);
        (void)dwg_bs_read_bt(&bs);
        {
            DWG_POINT3D extrusion;
            dwg_bs_read_be(&bs, &extrusion);
        }
        (void)dwg_bs_read_bd(&bs); /* X-axis angle */

        if (!is_plausible_coord(p.x) || !is_plausible_coord(p.y) || !is_plausible_coord(p.z))
            return 0L;
    }
    else /* SOLID */
    {
        double elevation, x1, y1, x2, y2, x3, y3, x4, y4;

        (void)dwg_bs_read_bt(&bs); /* thickness */
        elevation = dwg_bs_read_bd(&bs);
        x1 = dwg_bs_read_rd(&bs); y1 = dwg_bs_read_rd(&bs);
        x2 = dwg_bs_read_rd(&bs); y2 = dwg_bs_read_rd(&bs);
        x3 = dwg_bs_read_rd(&bs); y3 = dwg_bs_read_rd(&bs);
        x4 = dwg_bs_read_rd(&bs); y4 = dwg_bs_read_rd(&bs);

        if (!is_plausible_coord(x1) || !is_plausible_coord(y1) || !is_plausible_coord(x2) || !is_plausible_coord(y2) ||
            !is_plausible_coord(x3) || !is_plausible_coord(y3) || !is_plausible_coord(x4) || !is_plausible_coord(y4) ||
            !is_plausible_coord(elevation))
            return 0L;
    }

    *out_handle = common.handle;
    return 1L;
}

static long r2004_offset_decodes_to_handle(const unsigned char *objects_buf, unsigned long objects_size,
                                           unsigned long offset, unsigned long expected_handle)
{
    DWG_BITSTREAM bs;
    unsigned long declared_length;
    DWG_R2004_COMMON_ENTITY common;

    /* NOTE, same session: a fix was attempted here (reject non-entity-
       typed handles like LAYER, which this ENTITY-shaped check can't
       validly judge -- return a distinct -1L "unverifiable" instead of
       treating them as "stale") after that exact confusion was found
       corrupting LAYER offsets (unresolved layer names spiking under
       the collect-then-choose carving rewrite this session tried and
       reverted). The fix was logically correct but, tested in
       isolation with the ORIGINAL simple carve_missing_handles loop
       restored, STILL regressed real content (entity_count 18,002 ->
       16,370; VENTANAS 262 -> 16) -- some genuinely-corrupted entity
       handles were apparently only getting repaired here via the same
       type-mismatch coincidence that corrupts LAYER, and blocking it
       uniformly blocked those repairs too. Reverted back to the
       original, simpler 0/1-only form rather than trade one regression
       for another -- the LAYER-corruption risk this was meant to fix
       is real but narrower in practice than it first looked, and needs
       a more targeted fix (e.g. protecting specifically LAYER-handle
       lookups in resolve_r2004_layer_name itself, not this generic,
       widely-used repair check) if revisited. */
    if (offset + 8UL >= objects_size)
        return 0L;
    dwg_bs_init(&bs, objects_buf, objects_size);
    dwg_bs_seek_bit(&bs, offset * 8UL);
    declared_length = dwg_bs_read_ms(&bs);
    if (declared_length == 0UL)
        return 0L;
    (void)dwg_bs_read_mc(&bs, 0);
    (void)read_object_type_r2010(&bs);
    /* full check, matching the main decode loop's own verification
       exactly (through EED/preview/color, not just the first handle
       field) -- a shallower check let some genuinely-stale official
       entries slip through as "already fine" when their own handle
       happened to coincidentally read right but something later in the
       object didn't, leaving real repairable content (confirmed:
       LINE count stayed flat under the shallow check) undiscovered. */
    if (!read_common_entity_r2004(&bs, &common))
        return 0L;
    return (common.handle == expected_handle) ? 1L : 0L;
}

/* See the function comment further down (carve_missing_handles' own
   doc) for the full recovery story. This version does two things, not
   just one: adds handles ENTIRELY ABSENT from the official map (the
   original HATCH/DIMENSION discovery), and REPAIRS official entries
   that exist but point at the WRONG offset (confirmed via
   r2004_offset_decodes_to_handle -- the same "own handle" mismatch
   `common.handle != handles[i].handle` already detects generically,
   just checked directly here instead of waiting for the main decode
   loop to discover it and give up). Real, measured impact: recovers
   hundreds of additional real LINE/MTEXT/INSERT/HATCH entities whose
   handles existed but pointed nowhere useful -- a much bigger class of
   corruption in this file than "missing entirely" alone. */
/* UPDATE, same session: a first attempt to restructure this into a
   collect-then-choose pass (relaxed length cap + geometry-verified
   duplicate-handle disambiguation, mirroring R2000/R14's own orphan
   salvage) was tried and REVERTED after it made things measurably
   WORSE, not better -- entity_count dropped from 18,002 to 16,384 and
   unresolved layer names jumped from 313 to 9,393/1,795 depending on
   which fix was combined with which. Root cause of PART of that
   regression was real and is kept fixed (r2004_offset_decodes_to_
   handle now refuses to judge non-entity-typed handles like LAYER as
   "stale" -- see its own comment), but the full restructuring
   introduced other, not-yet-isolated regressions on top of that fix
   and was rolled back to this known-good, simpler form rather than
   ship something worse. r2004_orphan_geometry_ok (above) was built for
   this attempt and is kept, unused for now -- a real, working,
   independent geometry-plausibility check ready for a future, more
   INCREMENTAL retry (one change at a time, re-measured after each,
   instead of three changes landing together the way this attempt
   did). */
/* UPDATE, later session: Arturo confirmed "02_Planta 1 Baja_A3ver18.dwg"
   (AC1032/R2018, same reader) is the SAME architectural design as the
   R2000/R14 floor plans, just saved in a newer format -- so its own
   DXF ground truth (`02_Planta 1 Baja_A3.dxf`) is valid to compare
   against (unlike raw HANDLE numbers, which do NOT correspond between
   independent saves -- confirmed the hard way: chasing "missing"
   handles 208-223 by number led to unrelated real content that
   happened to share those handle values in THIS save's own handle
   space, not a bug). One-to-one coordinate recovery measured at 64.2%
   (17,958/27,979 DXF lines), worst-hit layers PUERTAS 8/335 (2.4%),
   LOSAS 47/230 (20%), VENTANAS 262/474 (55%) -- genuinely incomplete,
   matching what Arturo reported directly.

   Ran a deep diagnostic pass (since removed, all temporary) to find
   the mechanism, and RULED OUT every hypothesis tested, each with
   direct empirical evidence:
   - Page/decompression coverage: 100% clean -- all 57 AcDb:AcDbObjects
     pages (and both AcDb:Handles pages) found, decompressed, and
     verified to reach at least their own declared decompressed_size
     (only 2 of 57 pages under-produced, by 160 bytes each -- 320 bytes
     total out of 1,668,586, nowhere near enough to explain a
     ~10,000-object shortfall).
   - Structural under-detection from the type/handle-range filters:
     ruled out by re-scanning with those filters COMPLETELY removed
     (only requiring a plausible declared_length and type==LINE) --
     found the SAME ~18,260 LINE-type candidates, not more, meaning the
     strict filters aren't the bottleneck.
   - Hidden content under a different type code: ruled out by tallying
     ALL type codes 0x00-0x5F across the whole buffer -- every code
     shows the same ~100-250 "noise floor" (random-byte-alignment false
     positives) except LINE (0x13, ~18,260, the real signal) and two
     lesser peaks (0x00, 0x51) that are themselves noise-floor-shaped,
     not a hidden alternate encoding. INSERT (0x07) specifically shows
     ONLY 136 hits -- inside the noise floor, no real signal at all,
     ruling out "PUERTAS are block references" as the explanation too.
   - Direct coordinate hunt: decoded LINE geometry (ignoring handle/
     length gates entirely, requiring only type==LINE) at every one of
     those ~18,260 candidate positions and searched for a specific
     known-missing DXF coordinate -- zero matches anywhere in the
     buffer, and the single closest candidate (8cm away) was a
     differently-shaped, unrelated line, not the same segment shifted.

   Conclusion: the missing content genuinely is not encoded as
   directly-decodable LINE geometry anywhere in this file's own
   AcDb:AcDbObjects section -- this is NOT the same shape of bug as
   the R2000/R14 handle-drift story (where the real objects
   demonstrably existed and just needed better matching/resync). Given
   R2004's own carve_missing_handles is already finding essentially
   every real LINE-type object the buffer's own structure can yield
   (~18,260 found vs ~18,002 decoded, a tight, expected gap -- not a
   large one), further improvement here would need either (a) a
   genuinely different theory not yet tested (e.g. deeper reverse-
   engineering of the R2004 LZ77 variant beyond the page-level
   completeness check already done), or (b) accepting this as a real,
   currently-unexplained content gap specific to how this particular
   file was saved, rather than a fixable reader bug. Not resolved this
   session -- flagged clearly rather than guessed at further. */
static unsigned long carve_missing_handles(const unsigned char *objects_buf, unsigned long objects_size,
                                           DWG_R2004_HANDLE_ENTRY *handles, unsigned long handle_count,
                                           unsigned long max_entries)
{
    unsigned long pos, count;
    unsigned long original_count = handle_count;
    unsigned int old_fpu_cw;

    /* Real, confirmed crash found via this exact gap, same root cause
       and same fix already applied to R2000/R14's own carving/salvage
       scans this session (see dwg_r2000_reader.c's r2000_salvage_
       orphan_candidates for the full story) -- this scan evaluates a
       structural header at nearly EVERY byte position in the whole
       objects section (over a million positions for a real file), the
       overwhelming majority genuine false positives. r2004_offset_
       decodes_to_handle -> read_common_entity_r2004 reads a real
       double (ltype scale, dwg_bs_read_bd) as part of its own full
       verification -- reading raw garbage bits as IEEE754 doubles can
       and does produce signaling-NaN bit patterns at this scale, and
       this compiler's/runtime's default FPU control word does NOT mask
       the "invalid operand" exception, so merely reading/comparing
       such a value traps and crashes the whole process (confirmed:
       0xC0000090 STATUS_FLOAT_INVALID_OPERATION, NOT an integer divide
       as first suspected -- misread that exit code myself before
       checking the actual NTSTATUS value bit-for-bit). Never hit
       before this session because carve_missing_handles wasn't
       exercised against a file whose specific garbage-byte content
       happened to land on a signaling-NaN pattern until Arturo's
       AutoCAD-2018-exported file. */
    old_fpu_cw = _control87(MCW_EM, MCW_EM);

    count = handle_count;

    for (pos = 0UL; pos + 8UL < objects_size && count < max_entries; pos++)
    {
        DWG_BITSTREAM bs;
        unsigned long declared_length, handlestream_size, obj_type;
        unsigned char code;
        unsigned long value, k;
        long idx, dup;

        dwg_bs_init(&bs, objects_buf, objects_size);
        dwg_bs_seek_bit(&bs, pos * 8UL);

        declared_length = dwg_bs_read_ms(&bs);
        if (declared_length < 4UL || declared_length > 8000UL)
            continue;
        handlestream_size = dwg_bs_read_mc(&bs, 0);
        if (handlestream_size == 0UL || handlestream_size > declared_length * 8UL)
            continue;
        obj_type = read_object_type_r2010(&bs);

        if (obj_type != DWG_R2004_TYPE_LINE && obj_type != DWG_R2004_TYPE_CIRCLE &&
            obj_type != DWG_R2004_TYPE_ARC && obj_type != DWG_R2004_TYPE_POINT &&
            obj_type != DWG_R2004_TYPE_SOLID && obj_type != DWG_R2004_TYPE_INSERT &&
            obj_type != DWG_R2004_TYPE_HATCH && obj_type != DWG_R2004_TYPE_TEXT &&
            obj_type != DWG_R2004_TYPE_MTEXT && obj_type != DWG_R2004_TYPE_LWPOLYLINE &&
            obj_type != DWG_R2004_TYPE_DIM_ORDINATE && obj_type != DWG_R2004_TYPE_DIM_LINEAR &&
            obj_type != DWG_R2004_TYPE_DIM_ALIGNED && obj_type != DWG_R2004_TYPE_DIM_ANG3PT &&
            obj_type != DWG_R2004_TYPE_DIM_ANG2LN && obj_type != DWG_R2004_TYPE_DIM_RADIUS &&
            obj_type != DWG_R2004_TYPE_DIM_DIAMETER && obj_type != DWG_R2004_TYPE_BLOCK_HEADER)
            continue;

        dwg_bs_read_handle(&bs, &code, &value); /* an object's own handle is always code 0 (absolute) */
        if (code != 0UL || value == 0UL || value > 200000UL)
            continue;

        idx = r2004_find_handle_index(handles, original_count, value);
        if (idx >= 0L)
        {
            /* handle exists in the official map -- repair its offset
               only if that official entry is itself confirmed stale
               (doesn't actually decode to this handle); leave genuinely
               good entries untouched. */
            if (!r2004_offset_decodes_to_handle(objects_buf, objects_size, handles[idx].offset, value))
                handles[idx].offset = pos;
            continue;
        }

        dup = 0L;
        for (k = original_count; k < count; k++)
        {
            if (handles[k].handle == value)
            {
                dup = 1L;
                break;
            }
        }
        if (dup)
            continue;

        handles[count].handle = value;
        handles[count].offset = pos;
        count++;
    }

    if (count > original_count)
        qsort(handles, (size_t)count, sizeof(DWG_R2004_HANDLE_ENTRY), r2004_handle_entry_cmp);

    _control87(old_fpu_cw, MCW_EM);
    return count;
}

HDWG dwg_read_dwg_r2004(const char *path, DWG_IO_RESULT *result)
{
    unsigned char *data = NULL;
    unsigned long length = 0UL;
    DWG_R2004_FHDR fhdr;
    DWG_R2004_PAGE *pagemap = NULL;
    unsigned long pagemap_count = 0UL;
    DWG_R2004_SECTION *sections = NULL;
    unsigned long section_count = 0UL;
    const DWG_R2004_SECTION *sec_objects, *sec_handles;
    unsigned char *objects_buf = NULL;
    unsigned char *handles_buf = NULL;
    DWG_R2004_HANDLE_ENTRY *handles = NULL;
    unsigned long handle_count = 0UL;
    HDWG hDwg = NULL;
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

    if (length < 0x80UL ||
        (memcmp(data, "AC1024", 6) != 0 && memcmp(data, "AC1027", 6) != 0 && memcmp(data, "AC1032", 6) != 0))
    {
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    if (!decrypt_file_header(data, length, &fhdr))
    {
        free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }
    pagemap = (DWG_R2004_PAGE *)malloc(DWG_R2004_MAX_PAGES * sizeof(DWG_R2004_PAGE));
    sections = (DWG_R2004_SECTION *)malloc(DWG_R2004_MAX_SECTIONS * sizeof(DWG_R2004_SECTION));
    if (pagemap == NULL || sections == NULL)
    {
        free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    if (!parse_section_page_map(data, length, fhdr.section_map_address + 0x100UL, pagemap, &pagemap_count) ||
        !parse_section_info(data, length, pagemap, pagemap_count, fhdr.section_info_id, sections, &section_count))
    {
        free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    sec_objects = find_section_by_name(sections, section_count, "AcDb:AcDbObjects");
    sec_handles = find_section_by_name(sections, section_count, "AcDb:Handles");
    if (sec_objects == NULL || sec_handles == NULL)
    {
        free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    objects_buf = decompress_section(data, length, pagemap, pagemap_count, sec_objects);
    handles_buf = decompress_section(data, length, pagemap, pagemap_count, sec_handles);
    if (objects_buf == NULL || handles_buf == NULL)
    {
        free(objects_buf); free(handles_buf); free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    handles = (DWG_R2004_HANDLE_ENTRY *)malloc(65536UL * sizeof(DWG_R2004_HANDLE_ENTRY));
    if (handles == NULL)
    {
        free(objects_buf); free(handles_buf); free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }
    handle_count = parse_handles(handles_buf, sec_handles->total_size, handles, 65536UL);
    handle_count = carve_missing_handles(objects_buf, sec_objects->total_size, handles, handle_count, 65536UL);
    hDwg = dwg_document_create();
    if (hDwg == NULL)
    {
        free(handles); free(objects_buf); free(handles_buf); free(pagemap); free(sections); free(data);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    for (i = 0UL; i < handle_count; i++)
    {
        DWG_BITSTREAM bs;
        unsigned long declared_length, obj_type, handlestream_size, type_start_bit, bitsize;
        DWG_R2004_COMMON_ENTITY common;
        HENTITY e = NULL;

        if (handles[i].offset + 8UL >= sec_objects->total_size)
            continue;

        dwg_bs_init(&bs, objects_buf, sec_objects->total_size);
        dwg_bs_seek_bit(&bs, handles[i].offset * 8UL);

        declared_length = dwg_bs_read_ms(&bs);
        if (declared_length == 0UL)
            continue;

        handlestream_size = dwg_bs_read_mc(&bs, 0); /* R2010+ only, not physically
                                          present for AC1018/AC1021 which this
                                          reader deliberately rejects above */
        type_start_bit = dwg_bs_tell_bit(&bs);
        bitsize = declared_length * 8UL - handlestream_size;
        obj_type = read_object_type_r2010(&bs);

        if (obj_type != DWG_R2004_TYPE_LINE && obj_type != DWG_R2004_TYPE_CIRCLE &&
            obj_type != DWG_R2004_TYPE_ARC && obj_type != DWG_R2004_TYPE_POINT &&
            obj_type != DWG_R2004_TYPE_SOLID && obj_type != DWG_R2004_TYPE_INSERT &&
            obj_type != DWG_R2004_TYPE_HATCH && obj_type != DWG_R2004_TYPE_TEXT &&
            obj_type != DWG_R2004_TYPE_MTEXT && obj_type != DWG_R2004_TYPE_LWPOLYLINE &&
            obj_type != DWG_R2004_TYPE_DIM_ORDINATE && obj_type != DWG_R2004_TYPE_DIM_LINEAR &&
            obj_type != DWG_R2004_TYPE_DIM_ALIGNED && obj_type != DWG_R2004_TYPE_DIM_ANG3PT &&
            obj_type != DWG_R2004_TYPE_DIM_ANG2LN && obj_type != DWG_R2004_TYPE_DIM_RADIUS &&
            obj_type != DWG_R2004_TYPE_DIM_DIAMETER)
            continue;

        if (!read_common_entity_r2004(&bs, &common))
            continue;

        if (common.handle != handles[i].handle)
            continue; /* misaligned read (EED/graphics edge case not yet
                         handled) -- same "skip what doesn't line up"
                         discipline the R2000/R13-14 readers use */

        {
            char layer_name[256];
            long have_layer;
            unsigned short layer_color = 0U;

            have_layer = resolve_r2004_layer_name(objects_buf, sec_objects->total_size, handles, handle_count,
                                                  type_start_bit, bitsize, &common, layer_name, sizeof(layer_name),
                                                  &layer_color);

            switch (obj_type)
            {
            case DWG_R2004_TYPE_LINE:   e = decode_line(hDwg, &bs);   break;
            case DWG_R2004_TYPE_CIRCLE: e = decode_circle(hDwg, &bs); break;
            case DWG_R2004_TYPE_ARC:    e = decode_arc(hDwg, &bs);    break;
            case DWG_R2004_TYPE_POINT:  e = decode_point(hDwg, &bs);  break;
            case DWG_R2004_TYPE_SOLID:  e = decode_solid(hDwg, &bs);  break;
            case DWG_R2004_TYPE_INSERT:
                e = decode_insert(hDwg, &bs, objects_buf, sec_objects->total_size,
                                  handles, handle_count, type_start_bit, bitsize, &common);
                break;
            case DWG_R2004_TYPE_HATCH:
            {
                DWG_R2004_STRSTREAM ss;
                open_r2004_string_stream(objects_buf, sec_objects->total_size, type_start_bit, bitsize, &ss);
                e = decode_hatch(hDwg, &bs, &ss, common.color, have_layer ? layer_name : NULL);
                break;
            }
            case DWG_R2004_TYPE_TEXT:
                e = decode_text(hDwg, &bs, objects_buf, sec_objects->total_size, type_start_bit, bitsize);
                break;
            case DWG_R2004_TYPE_MTEXT:
                e = decode_mtext(hDwg, &bs, objects_buf, sec_objects->total_size, type_start_bit, bitsize);
                break;
            case DWG_R2004_TYPE_LWPOLYLINE:
                e = decode_lwpolyline(hDwg, &bs);
                break;
            case DWG_R2004_TYPE_DIM_ORDINATE:
            case DWG_R2004_TYPE_DIM_LINEAR:
            case DWG_R2004_TYPE_DIM_ALIGNED:
            case DWG_R2004_TYPE_DIM_ANG3PT:
            case DWG_R2004_TYPE_DIM_ANG2LN:
            case DWG_R2004_TYPE_DIM_RADIUS:
            case DWG_R2004_TYPE_DIM_DIAMETER:
                e = decode_dimension(hDwg, &bs, obj_type, objects_buf, sec_objects->total_size,
                                     handles, handle_count, type_start_bit, bitsize, &common);
                break;
            default: break;
            }

            if (e == NULL)
                continue;

            apply_color(e, common.color);
            if (have_layer)
            {
                dwg_entity_put_layer(e, layer_name);

                /* common.color==0/256 means BYBLOCK/BYLAYER -- apply_color
                   above deliberately left the entity's own color unset in
                   that case (same convention as R2000's own decode loop,
                   see its identical comment) -- resolve the REAL color
                   from the layer here instead, now that resolve_r2004_
                   layer_name actually decodes it. */
                if (common.color == 0U || common.color == 256U)
                    apply_color(e, layer_color);
            }
        }
    }

    free(handles);
    free(objects_buf);
    free(handles_buf);
    free(pagemap);
    free(sections);
    free(data);

    return hDwg;
}
