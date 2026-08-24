#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dwg_r2000_reader.h"
#include "dwg_bitstream.h"

static unsigned short rd_rs_le(const unsigned char *p)
{
    return (unsigned short)(p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long rd_rl_le(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Object-map section sizes/CRCs are the one place in this format that
   is big-endian -- see the header comment in dwg_r2000_reader.h. */
static unsigned short rd_rs_be(const unsigned char *p)
{
    return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

long dwg_r2000_parse_header(const unsigned char *data, unsigned long length,
                            DWG_R2000_HEADER *out)
{
    unsigned long off;
    unsigned long i;

    if (data == NULL || out == NULL || length < 0x19UL)
        return 0L;

    /* File header (version string through the section-locator records
       and trailing sentinel) is byte-identical for R13/R14/R2000 --
       confirmed empirically against a real R14 file in
       reverse/DWG_R1314_format_reference.md (the ODA spec's own R13-R15
       chapter documents this as one shared file structure; only the
       bit-level OBJECT encoding differs by version, handled by the
       separate R13/14 vs R2000 entity readers). Accepting all three
       version strings here lets dwg_r1314_reader.c reuse this same
       header/object-map parser instead of duplicating it. */
    if (memcmp(data, "AC1015", 6) != 0 &&
        memcmp(data, "AC1012", 6) != 0 &&
        memcmp(data, "AC1014", 6) != 0)
        return 0L;

    memcpy(out->version, data, 6UL);
    out->version[6] = '\0';

    out->image_seeker = rd_rl_le(data + 0x0D);
    out->codepage = rd_rs_le(data + 0x13);
    out->record_count = rd_rl_le(data + 0x15);

    if (out->record_count > (unsigned long)DWG_R2000_MAX_RECORDS)
        return 0L; /* more records than we have room for -- malformed or a version we don't expect */

    off = 0x19UL;
    for (i = 0UL; i < out->record_count; i++)
    {
        if (off + 9UL > length)
            return 0L;

        out->records[i].record_number = data[off];
        out->records[i].seeker = rd_rl_le(data + off + 1UL);
        out->records[i].size = rd_rl_le(data + off + 5UL);
        off += 9UL;
    }

    return 1L;
}

const DWG_R2000_SECTION_RECORD *dwg_r2000_find_record(const DWG_R2000_HEADER *header,
                                                       unsigned char record_number)
{
    unsigned long i;

    if (header == NULL)
        return NULL;

    for (i = 0UL; i < header->record_count; i++)
    {
        if (header->records[i].record_number == record_number)
            return &header->records[i];
    }

    return NULL;
}

static long objmap_push(DWG_R2000_OBJMAP *map, unsigned long handle, unsigned long location,
                        unsigned long *capacity)
{
    if (map->count == *capacity)
    {
        unsigned long new_capacity = (*capacity == 0UL) ? 16UL : (*capacity * 2UL);
        DWG_R2000_OBJMAP_ENTRY *new_entries =
            (DWG_R2000_OBJMAP_ENTRY *)realloc(map->entries, (size_t)new_capacity * sizeof(DWG_R2000_OBJMAP_ENTRY));

        if (new_entries == NULL)
            return 0L;

        map->entries = new_entries;
        *capacity = new_capacity;
    }

    map->entries[map->count].handle = handle;
    map->entries[map->count].location = location;
    map->count++;

    return 1L;
}

/* Real, confirmed bug (not a decode error -- every MC/MS/BS read
   involved was hand-verified byte-for-byte correct): for SOME object-
   map entries in real, large, BIM/Revit-exported files, the delta-
   accumulated `location` lands close to but not exactly at a real
   object's start -- confirmed on a genuine sample (`02_Planta 1
   Baja_A3.dwg`) where the computed location for one entry landed 24
   bytes before a small, otherwise-plausible non-LINE object, with a
   dense, clean run of real LINE objects (~50-60 bytes apart) both
   before and after it -- the delta itself isn't wildly wrong, just
   off by a small amount, and because `last_loc`/`last_handle` are
   running accumulators, that one small error invalidates almost
   everything read afterward (~76% of a 28k-object map in that file).

   Two weaker filters were tried and abandoned before this one: a
   plain MS-length + BS-type plausibility check (random bytes satisfy
   loose type/length ranges FAR too easily -- BS's own 2-bit "11"
   prefix alone means value=256 with no further bits read, so ~1/4 of
   ALL byte positions trivially "pass" as type=256), and the same
   check tightened to R2000's real fixed-type ranges (LibreDWG's own
   dwg.h: fixed types only occupy 1-82 plus PROXY_ENTITY=498/
   PROXY_OBJECT=499, class types from 500+) chained 2 deep -- better,
   but still landed on coincidental matches often enough to actively
   make things WORSE (entity_count dropped to 3), and assumed
   physically-adjacent bytes belong to the object-map's own "next"
   entry, which isn't generally true (object-map order is handle
   order, not physical file order).

   The real fix, found by fetching LibreDWG's actual decode.c (`src/
   decode.c`, the R13-R2000 object-map loop around its `handles_section`
   label): AutoCAD's own reader does NOT trust the accumulated
   handle/location deltas blindly either -- after decoding each
   object, it RE-SYNCS `last_handle` from that object's own,
   just-decoded, ACTUAL handle field (`dwg->object[dwg->num_objects-1]
   .handle.value`), not from the running `handleoff` sum. This engine
   reads the object map as a separate first pass before any entity
   decoding (unlike LibreDWG's single interleaved pass), so instead of
   re-syncing after the fact, this checks the SAME ground truth
   proactively: every real R2000 object's Common Entity/Object Data
   begins with `obj_size (RL) + handle (H)` right after the MS-length/
   BS-type header (confirmed against this engine's own
   read_common_entity_data in dwg_r2000_entity_reader.c) -- so a
   candidate location is trustworthy ONLY if its own embedded handle
   field EXACTLY equals the handle the delta-accumulator already
   independently computed for this entry (`handleoff` sums, which
   track correctly on their own -- confirmed empirically, only
   `location` drifts). Matching a full handle value by coincidence is
   astronomically less likely than a loose type/length pass, making
   this the actually-selective check the two earlier attempts weren't. */
static long r2000_offset_decodes_to_handle(const unsigned char *data, unsigned long length,
                                           unsigned long loc, unsigned long expected_handle)
{
    DWG_BITSTREAM bs;
    unsigned char code;
    unsigned long value;

    if (loc + 16UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    (void)dwg_bs_read_ms(&bs);  /* declared length */
    (void)dwg_bs_read_bs(&bs);  /* object type */
    (void)dwg_bs_read_rl(&bs);  /* obj_size, in BITS -- not used for the check itself */
    dwg_bs_read_handle(&bs, &code, &value);

    return (code == 0U && value == expected_handle) ? 1L : 0L;
}

/* The per-entry "search a window around the wrong location" approach
   above works, but is fundamentally the wrong shape: it's O(entries x
   window), and testing showed window size stopped mattering past
   ~200,000 bytes (500,000 gave byte-identical results at 7x the
   cost) -- meaning the entries it still can't fix aren't "just
   outside the window", they're not reachable by local search AT ALL
   from where the accumulator lands. The real object could be
   ANYWHERE in the file relative to a badly-drifted accumulator.

   Mirrors this engine's R2004+ carving technique instead (see
   dwg_r2004_entity_reader.c's carve_missing_handles): scan the WHOLE
   buffer ONCE for every self-consistent object header (same MS+BS+RL+H
   structural read as above, sane bounds on handle/type/length),
   collect (handle, location) pairs into a sorted index, and look up
   each object-map entry's expected handle directly via binary search
   -- O(file_size) once instead of O(entries x window), AND finds the
   real object no matter how far the accumulator has drifted, since
   it doesn't search relative to a (possibly badly wrong) starting
   point at all. */
typedef struct
{
    unsigned long handle;
    unsigned long location;
} R2000_CARVE_ENTRY;

static int r2000_carve_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const R2000_CARVE_ENTRY *)a)->handle;
    unsigned long hb = ((const R2000_CARVE_ENTRY *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

static R2000_CARVE_ENTRY *r2000_build_carve_index(const unsigned char *data, unsigned long length,
                                                   unsigned long *out_count)
{
    unsigned long pos, count = 0UL, capacity = 65536UL;
    R2000_CARVE_ENTRY *arr = (R2000_CARVE_ENTRY *)malloc(capacity * sizeof(R2000_CARVE_ENTRY));

    *out_count = 0UL;
    if (arr == NULL)
        return NULL;

    for (pos = 0UL; pos + 16UL < length; pos++)
    {
        DWG_BITSTREAM bs;
        unsigned long len;
        unsigned short type;
        unsigned char code;
        unsigned long value;

        dwg_bs_init(&bs, data, length);
        dwg_bs_seek_bit(&bs, pos * 8UL);
        len = dwg_bs_read_ms(&bs);
        /* PROXY_ENTITY/PROXY_OBJECT embed their entire opaque data
           blob in this same declared length -- a real Revit-exported
           furniture/fixture proxy can legitimately be much larger
           than any ordinary LINE/ARC/TEXT object, so the cap here
           has to be generous (bounded mainly by the file itself)
           rather than tuned to "normal" small entities. */
        if (len < 4UL || len > length)
            continue;
        type = dwg_bs_read_bs(&bs);
        if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type < 5000U)))
            continue;
        (void)dwg_bs_read_rl(&bs);
        dwg_bs_read_handle(&bs, &code, &value);
        if (code != 0U || value == 0UL || value > 5000000UL)
            continue;

        if (count >= capacity)
        {
            R2000_CARVE_ENTRY *bigger;
            capacity *= 2UL;
            bigger = (R2000_CARVE_ENTRY *)realloc(arr, capacity * sizeof(R2000_CARVE_ENTRY));
            if (bigger == NULL)
                break; /* keep what we have so far rather than losing everything */
            arr = bigger;
        }
        arr[count].handle = value;
        arr[count].location = pos;
        count++;
    }

    qsort(arr, (size_t)count, sizeof(R2000_CARVE_ENTRY), r2000_carve_cmp);
    *out_count = count;
    return arr;
}

static long r2000_carve_lookup(const R2000_CARVE_ENTRY *arr, unsigned long count,
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

long dwg_r2000_parse_object_map(const unsigned char *data, unsigned long length,
                                unsigned long seeker, unsigned long size,
                                DWG_R2000_OBJMAP *out)
{
    unsigned long pos = seeker;
    unsigned long capacity = 0UL;
    unsigned long last_handle = 0UL;
    unsigned long last_loc = 0UL;
    R2000_CARVE_ENTRY *carve_index;
    unsigned long carve_count;

    out->entries = NULL;
    out->count = 0UL;

    if (data == NULL || length < seeker + size)
        return 0L;

    carve_index = r2000_build_carve_index(data, length, &carve_count);

    while (pos < seeker + size)
    {
        unsigned short section_size;
        unsigned long section_start;
        unsigned long section_end_byte;
        DWG_BITSTREAM bs;

        if (pos + 2UL > length)
            break;

        section_size = rd_rs_be(data + pos);
        section_start = pos;
        pos += 2UL;

        if (section_size == 2U)
            break; /* CRC-only trailer section: end of the object map */

        section_end_byte = section_start + section_size;
        if (section_end_byte > length || section_end_byte < pos + 2UL)
            break; /* malformed: section too small to hold its own trailing CRC */

        dwg_bs_init(&bs, data, length);
        dwg_bs_seek_bit(&bs, pos * 8UL);

        /* Bound is section_end_byte itself, NOT section_end_byte - 2:
           the trailing CRC lives OUTSIDE the declared section_size
           (confirmed empirically -- see DWG_R2000_format_reference.md's
           CRC section), so there's nothing to reserve room for here.
           The old "-2" silently dropped a section's last handle/
           location pair whenever it landed exactly at that boundary
           (byte-tight, no slack) -- never triggered on any real file
           tested so far (their last pair always had a byte or two of
           slack before the boundary), only surfaced once this engine
           started writing byte-tight object maps of its own. */
        while (dwg_bs_tell_bit(&bs) / 8UL < section_end_byte)
        {
            long hoff = dwg_bs_read_mc(&bs, 0); /* handle offsets are never negative */
            long loff = dwg_bs_read_mc(&bs, 1);

            unsigned long carved_loc;

            last_handle += (unsigned long)hoff;
            last_loc = (unsigned long)((long)last_loc + loff);

            /* Fast path: the accumulator already landed correctly (the
               overwhelming majority of entries, even in a corrupted
               file -- most of the map is fine, only certain stretches
               drift). Only fall back to the carved index -- built
               once, not per entry -- when it didn't. */
            if (!r2000_offset_decodes_to_handle(data, length, last_loc, last_handle) &&
                carve_index != NULL &&
                r2000_carve_lookup(carve_index, carve_count, last_handle, &carved_loc))
                last_loc = carved_loc;

            if (!objmap_push(out, last_handle, last_loc, &capacity))
            {
                free(carve_index);
                dwg_r2000_objmap_free(out);
                return 0L;
            }
        }

        pos = section_end_byte; /* skip the trailing 2-byte CRC (not validated here) */
        pos += 2UL;
    }

    free(carve_index);
    return 1L;
}

void dwg_r2000_objmap_free(DWG_R2000_OBJMAP *map)
{
    if (map == NULL)
        return;

    if (map->entries != NULL)
        free(map->entries);

    map->entries = NULL;
    map->count = 0UL;
}
