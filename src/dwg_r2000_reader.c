#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

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

static int r2000_objmap_entry_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const DWG_R2000_OBJMAP_ENTRY *)a)->handle;
    unsigned long hb = ((const DWG_R2000_OBJMAP_ENTRY *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
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
/* The type-range check below (used by both the fast-path check and
   the carve index) originally allowed the WHOLE class-type range
   (500-4999) as "plausible" -- but a real file only ever registers a
   HANDFUL of classes (confirmed via test_class_table_check.exe: 6 for
   `02_Planta 1 Baja_A3.dwg`, ending at type 505; 9 for the R14
   sibling, ending at 508). Any location whose read "type" lands
   anywhere in the unused remainder of that huge range (506-4999 here)
   is structurally self-consistent by pure chance, not a real object
   -- confirmed by a type tally showing real counts at types like 512,
   468, 492 etc. that have no registered class anywhere near them.
   Reads the file's own Classes section (R13-R15 layout, ODA spec) to
   learn the REAL upper bound instead of trusting the format's
   theoretical maximum -- falls back to the old permissive 4999 bound
   only if the class section itself can't be located/parsed (should
   never happen for a well-formed file, but a wrong bound here would
   only make filtering less strict, never crash). */
static unsigned short r2000_max_class_type(const unsigned char *data, unsigned long length)
{
    DWG_R2000_HEADER header;
    const DWG_R2000_SECTION_RECORD *crec;
    DWG_BITSTREAM cbs;
    unsigned long cpos, idx = 0UL;

    if (!dwg_r2000_parse_header(data, length, &header))
        return 4999U;

    crec = dwg_r2000_find_record(&header, 1U);
    if (crec == NULL)
        return 4999U;

    cpos = crec->seeker + 16UL;
    if (cpos + 4UL >= length)
        return 4999U;

    dwg_bs_init(&cbs, data, length);
    dwg_bs_seek_bit(&cbs, cpos * 8UL);
    (void)dwg_bs_read_rl(&cbs); /* class_size, not needed here */

    while (idx < 1000UL)
    {
        unsigned long classnum;
        char appname[128], cppname[128], dxfname[128];

        classnum = dwg_bs_read_bs(&cbs);
        if (classnum == 0UL || classnum > 10000UL)
            break;
        (void)dwg_bs_read_bs(&cbs); /* version */
        (void)dwg_bs_read_t(&cbs, appname, sizeof(appname));
        (void)dwg_bs_read_t(&cbs, cppname, sizeof(cppname));
        (void)dwg_bs_read_t(&cbs, dxfname, sizeof(dxfname));
        (void)dwg_bs_read_bit(&cbs);  /* wasazombie */
        (void)dwg_bs_read_bs(&cbs);   /* itemclassid */
        idx++;
    }

    return (idx == 0UL) ? 500U : (unsigned short)(500UL + idx - 1UL);
}

/* Real, confirmed bug found via a full type tally of the final,
   post-repair object map on `02_Planta 1 Baja_A3.dwg`: this check
   originally validated ONLY the handle match, discarding the type it
   had just read without looking at it. A type tally of the resulting
   locations showed ~5,800 entries (20% of the whole 28,192-entry map)
   landing on type=0, type=256 (the classic "BS 2-bit '11' prefix"
   artifact -- any random byte whose top two bits are "11" trivially
   decodes to 256 with zero further bits read), or other values with
   no registered class anywhere near this file's real 500-505 class
   range -- i.e. NOT real objects at all, just locations where the
   accumulated `last_loc` happened to also produce an accidental exact
   handle-value match at the H field position. Handle values in a
   dense file like this are small, tightly-clustered integers (not
   random 32-bit numbers), so an accidental exact match turned out to
   be far more likely than assumed when this check was first written.
   Fix: require the SAME type-range plausibility the carve index
   already enforces (r2000_build_carve_index below), not just the
   handle match -- keeps the fast path and the carved fallback at the
   same quality bar, so a bad match here now correctly falls through
   to carve_lookup (or stays visibly unresolved) instead of silently
   winning with garbage. */
static long r2000_offset_decodes_to_handle(const unsigned char *data, unsigned long length,
                                           unsigned long loc, unsigned long expected_handle,
                                           unsigned short max_class_type)
{
    DWG_BITSTREAM bs;
    unsigned long len;
    unsigned short type;
    unsigned char code;
    unsigned long value;

    if (loc + 16UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    len = dwg_bs_read_ms(&bs);
    if (len < 4UL || len > length)
        return 0L;
    type = dwg_bs_read_bs(&bs);
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;
    (void)dwg_bs_read_rl(&bs);  /* obj_size, in BITS -- not used for the check itself */
    dwg_bs_read_handle(&bs, &code, &value);

    return (code == 0U && value == expected_handle) ? 1L : 0L;
}

/* Same check, R14's real field order (Type -> Handle directly, no
   RL obj_size first). */
static long r1314_offset_decodes_to_handle_shared(const unsigned char *data, unsigned long length,
                                                   unsigned long loc, unsigned long expected_handle,
                                                   unsigned short max_class_type)
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
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;
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

/* Sorted VIEW of the shared raw candidate pool: carries the same
   (handle, location) plus the candidate's index in the shared raw[]/
   claimed[] arrays, so a lookup through either sorted view (by handle
   or by location) can check/set the SAME claimed-ness regardless of
   which view found it. See the uniqueness-enforcement comment above
   r2000_nearest_by_location below for why this exists. */
typedef struct
{
    unsigned long handle;
    unsigned long location;
    unsigned long raw_index;
} R2000_CARVE_SORT_ENTRY;

static int r2000_sort_cmp_handle(const void *a, const void *b)
{
    unsigned long ha = ((const R2000_CARVE_SORT_ENTRY *)a)->handle;
    unsigned long hb = ((const R2000_CARVE_SORT_ENTRY *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

static int r2000_sort_cmp_loc(const void *a, const void *b)
{
    unsigned long la = ((const R2000_CARVE_SORT_ENTRY *)a)->location;
    unsigned long lb = ((const R2000_CARVE_SORT_ENTRY *)b)->location;
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}

/* Structural candidate check, R2000 field order (MS -> BS type ->
   RL obj_size skip -> H handle) -- shared by carve-index building AND
   (new) the handle-DRIFT resync mechanism below, so both use exactly
   the same candidate definition. */
static long r2000_peek_any_handle(const unsigned char *data, unsigned long length, unsigned long loc,
                                  unsigned long *out_handle, unsigned short max_class_type)
{
    DWG_BITSTREAM bs;
    unsigned long len;
    unsigned short type;
    unsigned char code;
    unsigned long value;

    if (loc + 16UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    len = dwg_bs_read_ms(&bs);
    /* PROXY_ENTITY/PROXY_OBJECT embed their entire opaque data blob in
       this same declared length -- a real Revit-exported furniture/
       fixture proxy can legitimately be much larger than any ordinary
       LINE/ARC/TEXT object, so the cap here has to be generous
       (bounded mainly by the file itself) rather than tuned to
       "normal" small entities. */
    if (len < 4UL || len > length)
        return 0L;
    type = dwg_bs_read_bs(&bs);
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;
    (void)dwg_bs_read_rl(&bs);
    dwg_bs_read_handle(&bs, &code, &value);
    if (code != 0U || value == 0UL || value > 5000000UL)
        return 0L;

    *out_handle = value;
    return 1L;
}

/* Same idea, R14's real field order (MS -> BS type -> H handle
   directly, no RL obj_size before it -- see read_common_entity_data_
   r1314's own comment in dwg_r1314_entity_reader.c). A separate copy,
   not a shared function, deliberately -- this project already learned
   the hard way this session that a single R2000-shaped structural
   check silently misreads every R14 object (see r1314_peek_handle's
   own comment in dwg_r1314_entity_reader.c for that story). */
static long r1314_peek_any_handle_shared(const unsigned char *data, unsigned long length, unsigned long loc,
                                         unsigned long *out_handle, unsigned short max_class_type)
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
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;
    dwg_bs_read_handle(&bs, &code, &value);
    if (code != 0U || value == 0UL || value > 5000000UL)
        return 0L;

    *out_handle = value;
    return 1L;
}

/* Coordinate sanity threshold -- own local copy (not shared with
   dwg_r2000_entity_reader.c's or dwg_r1314_entity_reader.c's private
   copies) since this file has its own established pattern of keeping
   per-format structural logic self-contained rather than reaching
   into the entity readers. Same 10M value, same reasoning as both of
   those (see either one's own comment for the full story: a garbage-
   decoded coordinate like 1e101 is a reliable tell that a candidate
   isn't real, even when its header structurally validated). */
#define R2000_READER_MAX_PLAUSIBLE_COORD 1.0e7

static int r2000_reader_is_plausible_coord(double v)
{
    return v > -R2000_READER_MAX_PLAUSIBLE_COORD && v < R2000_READER_MAX_PLAUSIBLE_COORD;
}

/* Orphan-candidate geometry check, R2000 field order. Used by the
   salvage pass below (see its own comment for why it exists): decodes
   a candidate's ACTUAL entity-specific coordinate fields (for the
   coordinate-bearing types that dominate real content) and applies
   the same plausibility gate the real decoders use at final
   acceptance time -- if a candidate's own geometry looks genuinely
   real, it's trustworthy evidence independent of any handle/location
   matching, byte-proximity heuristic, or MC-delta accumulator. Returns
   1 (with *out_handle set) if this candidate is a coordinate-bearing
   type with plausible geometry, 0 otherwise (either a type this check
   doesn't verify, or verified-and-implausible). */
static long r2000_orphan_geometry_ok(const unsigned char *data, unsigned long length, unsigned long loc,
                                     unsigned short max_class_type, unsigned long *out_handle)
{
    DWG_BITSTREAM bs;
    unsigned long len;
    unsigned short type;
    unsigned char hcode;
    unsigned long hvalue;
    unsigned short eed_size;
    unsigned long preview_exists;
    unsigned long numreactors;

    if (loc + 16UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    len = dwg_bs_read_ms(&bs);
    if (len < 4UL || len > length)
        return 0L;
    type = dwg_bs_read_bs(&bs);
    if (type != 0x13U && type != 0x11U && type != 0x12U && type != 0x1BU)
        return 0L; /* only LINE/ARC/CIRCLE/POINT checked -- see r1314's analogous comment */
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;

    (void)dwg_bs_read_rl(&bs); /* obj_size_bits */
    dwg_bs_read_handle(&bs, &hcode, &hvalue);
    if (hcode != 0U || hvalue == 0UL || hvalue > 5000000UL)
        return 0L;

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
    {
        unsigned short remain = eed_size;
        while (remain != 0U)
        {
            unsigned char c; unsigned long v; unsigned short i;
            dwg_bs_read_handle(&bs, &c, &v);
            for (i = 0U; i < remain; i++) (void)dwg_bs_read_bits(&bs, 8UL);
            remain = dwg_bs_read_bs(&bs);
        }
    }
    preview_exists = dwg_bs_read_bit(&bs);
    if (preview_exists != 0UL)
    {
        unsigned long preview_size = dwg_bs_read_rl(&bs);
        if (preview_size > 0x7FFFFFFFUL) return 0L;
        dwg_bs_seek_bit(&bs, dwg_bs_tell_bit(&bs) + preview_size * 8UL);
    }

    (void)dwg_bs_read_bb(&bs); /* entmode */
    numreactors = dwg_bs_read_bl(&bs);
    if (numreactors > 1000UL) return 0L;
    (void)dwg_bs_read_bit(&bs); /* nolinks */
    (void)dwg_bs_read_bs(&bs);  /* color */
    (void)dwg_bs_read_bd(&bs);  /* ltscale */
    (void)dwg_bs_read_bb(&bs);  /* ltflags */
    (void)dwg_bs_read_bb(&bs);  /* plotstyleflags */
    (void)dwg_bs_read_bs(&bs);  /* invisibility */

    if (type == 0x13U) /* LINE: R2000's z-is-zero-bit + RD/DD scheme */
    {
        unsigned long zbit;
        double sx, sy, sz, ex, ey, ez;
        zbit = dwg_bs_read_bit(&bs);
        sx = dwg_bs_read_rd(&bs);
        ex = dwg_bs_read_dd(&bs, sx);
        sy = dwg_bs_read_rd(&bs);
        ey = dwg_bs_read_dd(&bs, sy);
        if (zbit == 0UL) { sz = dwg_bs_read_rd(&bs); ez = dwg_bs_read_dd(&bs, sz); }
        else { sz = 0.0; ez = 0.0; }
        if (!r2000_reader_is_plausible_coord(sx) || !r2000_reader_is_plausible_coord(sy) ||
            !r2000_reader_is_plausible_coord(sz) || !r2000_reader_is_plausible_coord(ex) ||
            !r2000_reader_is_plausible_coord(ey) || !r2000_reader_is_plausible_coord(ez))
            return 0L;
    }
    else /* ARC/CIRCLE/POINT: all plain 3BD-first */
    {
        DWG_POINT3D p;
        double extra = 0.0;
        dwg_bs_read_3bd(&bs, &p);
        if (type == 0x11U || type == 0x12U) /* ARC/CIRCLE also have a radius right after */
            extra = dwg_bs_read_bd(&bs);
        if (!r2000_reader_is_plausible_coord(p.x) || !r2000_reader_is_plausible_coord(p.y) ||
            !r2000_reader_is_plausible_coord(p.z) ||
            ((type == 0x11U || type == 0x12U) && !r2000_reader_is_plausible_coord(extra)))
            return 0L;
        /* Real, confirmed false positive found via this exact gap:
           a huge fake CIRCLE (center exactly (0,0,0), radius exactly
           1.0, no DXF match, empty layer) rendered on top of a real
           floor plan (Arturo: "el circulo no existe"). BD's own
           bitstream encoding has a well-known 2-bit "short form" for
           the two special values 0.0 and 1.0 -- no further bits
           consumed. A misaligned garbage read has a real, non-trivial
           chance of landing on that 2-bit opcode for EACH field it
           reads, so multiple independent fields all coming back
           exactly (0,0,0)/1.0 together is an easy coincidence for
           garbage but essentially impossible for genuine authored
           content (neither this file nor its sibling has any real
           entity anywhere near the world origin). Position-plausibility
           alone can't catch this -- (0,0,0) and 1.0 are each
           individually completely ordinary values -- so this checks
           the SPECIFIC "everything hit the default shorthand at once"
           signature instead, deliberately scoped to the orphan-salvage
           path only (the newest, least-trusted source, not the normal
           decode path real files everywhere else rely on). */
        if (p.x == 0.0 && p.y == 0.0 && p.z == 0.0)
            return 0L;
    }

    *out_handle = hvalue;
    return 1L;
}

/* Same check, R14's field order (handle right after type, no EED/
   preview skip needed before it; obj_size comes later; LINE uses
   plain 3BD like ARC/CIRCLE/POINT instead of R2000's z-bit+RD/DD
   scheme -- see read_common_entity_data_r1314's own comment in
   dwg_r1314_entity_reader.c for the full field-order derivation). */
static long r1314_orphan_geometry_ok(const unsigned char *data, unsigned long length, unsigned long loc,
                                     unsigned short max_class_type, unsigned long *out_handle)
{
    DWG_BITSTREAM bs;
    unsigned long len;
    unsigned short type;
    unsigned char hcode;
    unsigned long hvalue;
    unsigned short eed_size;
    unsigned long preview_exists;
    unsigned long numreactors;

    if (loc + 8UL >= length)
        return 0L;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);
    len = dwg_bs_read_ms(&bs);
    if (len < 4UL || len > length)
        return 0L;
    type = dwg_bs_read_bs(&bs);
    if (type != 0x13U && type != 0x11U && type != 0x12U && type != 0x1BU)
        return 0L;
    if (!((type >= 1U && type <= 0x52U) || type == 0x1F2U || type == 0x1F3U || (type >= 500U && type <= max_class_type)))
        return 0L;

    dwg_bs_read_handle(&bs, &hcode, &hvalue);
    if (hcode != 0U || hvalue == 0UL || hvalue > 5000000UL)
        return 0L;

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
    {
        unsigned short remain = eed_size;
        while (remain != 0U)
        {
            unsigned char c; unsigned long v; unsigned short i;
            dwg_bs_read_handle(&bs, &c, &v);
            for (i = 0U; i < remain; i++) (void)dwg_bs_read_bits(&bs, 8UL);
            remain = dwg_bs_read_bs(&bs);
        }
    }
    preview_exists = dwg_bs_read_bit(&bs);
    if (preview_exists != 0UL)
    {
        unsigned long preview_size = dwg_bs_read_rl(&bs);
        if (preview_size > 0x7FFFFFFFUL) return 0L;
        dwg_bs_seek_bit(&bs, dwg_bs_tell_bit(&bs) + preview_size * 8UL);
    }

    (void)dwg_bs_read_rl(&bs); /* obj_size_bits */
    (void)dwg_bs_read_bb(&bs); /* entmode */
    numreactors = dwg_bs_read_bl(&bs);
    if (numreactors > 1000UL) return 0L;
    (void)dwg_bs_read_bit(&bs); /* isbylayerlt */
    (void)dwg_bs_read_bit(&bs); /* nolinks */
    (void)dwg_bs_read_bs(&bs);  /* color */
    (void)dwg_bs_read_bd(&bs);  /* ltscale */
    (void)dwg_bs_read_bs(&bs);  /* invisibility */

    {
        DWG_POINT3D p, p2;
        double extra = 0.0;
        dwg_bs_read_3bd(&bs, &p);
        if (type == 0x13U) /* LINE: second point right after */
            dwg_bs_read_3bd(&bs, &p2);
        else
            p2.x = p2.y = p2.z = 0.0;
        if (type == 0x11U || type == 0x12U) /* ARC/CIRCLE: radius after center */
            extra = dwg_bs_read_bd(&bs);

        if (!r2000_reader_is_plausible_coord(p.x) || !r2000_reader_is_plausible_coord(p.y) ||
            !r2000_reader_is_plausible_coord(p.z))
            return 0L;
        if (type == 0x13U && (!r2000_reader_is_plausible_coord(p2.x) || !r2000_reader_is_plausible_coord(p2.y) ||
                              !r2000_reader_is_plausible_coord(p2.z)))
            return 0L;
        if ((type == 0x11U || type == 0x12U) && !r2000_reader_is_plausible_coord(extra))
            return 0L;

        /* Same real false-positive fixed in r2000_orphan_geometry_ok's
           own copy of this check (see its comment there for the full
           reasoning) -- BD's short-form 0.0/1.0 encoding means a
           misaligned garbage read can produce a suspiciously "clean"
           point at the exact world origin purely by chance. */
        if (p.x == 0.0 && p.y == 0.0 && p.z == 0.0)
            return 0L;
    }

    *out_handle = hvalue;
    return 1L;
}

/* One whole-file scan for every self-consistent candidate header,
   UNSORTED -- the shared ground truth both sorted views (by handle,
   by location) and the shared `claimed[]` uniqueness tracker below
   are built from/indexed against. */
static R2000_CARVE_ENTRY *r2000_build_raw_candidates(const unsigned char *data, unsigned long length,
                                                      unsigned long *out_count, unsigned short max_class_type,
                                                      long is_r1314)
{
    unsigned long pos, count = 0UL, capacity = 65536UL;
    R2000_CARVE_ENTRY *arr = (R2000_CARVE_ENTRY *)malloc(capacity * sizeof(R2000_CARVE_ENTRY));

    *out_count = 0UL;
    if (arr == NULL)
        return NULL;

    for (pos = 0UL; pos + 16UL < length; pos++)
    {
        unsigned long value;

        if (is_r1314)
        {
            if (!r1314_peek_any_handle_shared(data, length, pos, &value, max_class_type))
                continue;
        }
        else
        {
            if (!r2000_peek_any_handle(data, length, pos, &value, max_class_type))
                continue;
        }

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

    *out_count = count;
    return arr;
}

/* Builds a sorted VIEW over the raw candidates (see the struct's own
   comment) -- each entry keeps its raw[] index so a match found via
   EITHER view can check/set the SAME shared `claimed[]` slot. */
static R2000_CARVE_SORT_ENTRY *r2000_build_sorted_view(const R2000_CARVE_ENTRY *raw, unsigned long count,
                                                        int sort_by_location)
{
    unsigned long i;
    R2000_CARVE_SORT_ENTRY *view;

    if (count == 0UL)
        return NULL;

    view = (R2000_CARVE_SORT_ENTRY *)malloc(count * sizeof(R2000_CARVE_SORT_ENTRY));
    if (view == NULL)
        return NULL;

    for (i = 0UL; i < count; i++)
    {
        view[i].handle = raw[i].handle;
        view[i].location = raw[i].location;
        view[i].raw_index = i;
    }

    qsort(view, (size_t)count, sizeof(R2000_CARVE_SORT_ENTRY),
         sort_by_location ? r2000_sort_cmp_loc : r2000_sort_cmp_handle);
    return view;
}

/* Maps a raw candidate's index (as seen via the BY-HANDLE view's own
   `raw_index` field) to that SAME candidate's position in the
   BY-LOCATION view -- needed so a claim made while searching by
   handle (r2000_carve_lookup) can also update the location-indexed
   free-slot tracker (r2000_nearest_by_location's R2000_FREE_TRACKER),
   keeping both claiming mechanisms in agreement about what's taken. */
static long *r2000_build_raw_to_loc_pos(const R2000_CARVE_SORT_ENTRY *by_loc, unsigned long count)
{
    long *out;
    unsigned long i;

    if (count == 0UL)
        return NULL;

    out = (long *)malloc((size_t)count * sizeof(long));
    if (out == NULL)
        return NULL;

    for (i = 0UL; i < count; i++)
        out[by_loc[i].raw_index] = (long)i;

    return out;
}

/* Real, confirmed root cause found via a direct empirical test
   (test_handle_drift_check.exe, using the file's own DXF export as
   ground truth -- the DXF preserves every real object's DWG handle
   via standard group code 5): 76-86% of ALL delta-accumulated
   `last_handle` values across the WHOLE object map do not exist
   ANYWHERE in the DXF's real handle set at all. This is not a
   location-only drift -- the running handle accumulator itself goes
   wrong, almost certainly because a single early MC-decode edge case
   (or a genuinely different encoding convention this engine doesn't
   know about yet) throws off `last_handle`, and every entry after
   that point inherits the error (handleoff deltas are correct
   relative to a WRONG base, so they never self-correct).

   AutoCAD's own reader (LibreDWG's decode.c, `handles_section`) never
   has this problem because it re-syncs `last_handle` from each
   object's own real handle after EVERY decode -- this engine can't
   do that literally (object-map parsing is a separate first pass,
   before any entity decoding), but can do the structural equivalent:
   when neither the fast path nor a handle-keyed carve lookup finds a
   trustworthy match, search for the CLOSEST real, self-consistent
   object header BY LOCATION instead (ignoring what handle it claims
   to have), and if found, RESYNC both `last_handle` and `last_loc` to
   that object's own actual values -- getting the accumulator back on
   track for every entry that follows, not just patching this one. */
/* Real, confirmed regression found right after the resync mechanism
   above first landed: sorting the final object map by handle (see
   the sort fix later in this file) made results LOOK consistent, but
   a direct check against the file's own DXF (test_missing_lines_
   check.exe: for every DXF LINE handle, is it actually present, and
   as what type, in the final object map) found 12,104 of 27,979 real
   DXF-LINE handles simply MISSING from the map entirely, and a direct
   count of the final map's own location values (test_objmap_dup_
   check.exe) found only 16,219 UNIQUE locations among 28,192 total
   entries -- 42% of all entries point at a location ALREADY claimed
   by a different entry. Multiple different (wrong) accumulator
   positions were independently resyncing/carving to the SAME nearby
   "attractive" real object, silently crowding out that object's TRUE
   handle while leaving it permanently unrepresented in the map.

   Fix: track which raw candidates have already been assigned to some
   OTHER object-map entry (`claimed[]`, shared across BOTH the handle-
   keyed lookup below and this location-based one, indexed by each
   candidate's position in the common raw[] pool -- see R2000_CARVE_
   SORT_ENTRY's own comment), and never offer an already-claimed
   candidate to a second entry. Every successful match claims its slot
   before returning, so the object map's final (handle, location)
   pairs really are 1:1 with real objects, not many-to-one.

   Real performance concern with the obvious approach (expand outward
   from the binary-search insertion point, skipping claimed slots):
   on a large, heavily-drifted file (R14's 4MB, ~90,809 candidates,
   ~85% of ~57,937 entries needing resync) a big contiguous stretch of
   the by-location array can end up claimed, forcing later searches to
   walk arbitrarily far past it every time -- worst case O(n) per
   lookup, O(n^2) over the whole map. Fixed with a standard "nearest
   remaining slot" union-find (the same technique used for "allocate
   the closest free parking space" problems): two path-compressed
   arrays, `next_free`/`prev_free`, each initialized to the identity
   (`arr[i] = i`); finding the nearest free index in one direction is
   amortized near-O(1) (inverse-Ackermann) even after heavy claiming,
   and claiming index i is a single O(1) write (`arr[i] = i +/- 1`) --
   the expensive re-linking happens lazily, spread across future
   finds, not eagerly at claim time. */
static long r2000_uf_find(long *parent, long x)
{
    long root = x;
    while (parent[root] != root)
        root = parent[root];
    while (parent[x] != root)
    {
        long next = parent[x];
        parent[x] = root;
        x = next;
    }
    return root;
}

typedef struct
{
    long *next_free; /* size count+1; next_free[count] is a permanent "none further right" sentinel */
    long *prev_free;  /* size count+1, index shifted by +1 (index 0 = virtual "-1", "none further left") */
    long count;
} R2000_FREE_TRACKER;

static R2000_FREE_TRACKER *r2000_free_tracker_create(unsigned long count)
{
    R2000_FREE_TRACKER *ft = (R2000_FREE_TRACKER *)malloc(sizeof(R2000_FREE_TRACKER));
    long i;

    if (ft == NULL)
        return NULL;

    ft->count = (long)count;
    ft->next_free = (long *)malloc(((size_t)count + 1U) * sizeof(long));
    ft->prev_free = (long *)malloc(((size_t)count + 1U) * sizeof(long));
    if (ft->next_free == NULL || ft->prev_free == NULL)
    {
        free(ft->next_free);
        free(ft->prev_free);
        free(ft);
        return NULL;
    }

    for (i = 0L; i <= ft->count; i++)
    {
        ft->next_free[i] = i;
        ft->prev_free[i] = i;
    }

    return ft;
}

static void r2000_free_tracker_destroy(R2000_FREE_TRACKER *ft)
{
    if (ft == NULL)
        return;
    free(ft->next_free);
    free(ft->prev_free);
    free(ft);
}

static void r2000_free_tracker_claim(R2000_FREE_TRACKER *ft, long idx)
{
    ft->next_free[idx] = idx + 1L;      /* next find(idx) resolves past this slot */
    ft->prev_free[idx + 1L] = idx;      /* prev array is shifted by +1; resolves before this slot */
}

/* Nearest remaining (unclaimed) index to `target_loc`'s binary-search
   insertion point, in EITHER direction. Returns 0 if none remain at
   all. */
static long r2000_free_tracker_nearest(R2000_FREE_TRACKER *ft, const R2000_CARVE_SORT_ENTRY *by_loc,
                                       long start, unsigned long target_loc, long *out_idx)
{
    long next_idx = r2000_uf_find(ft->next_free, start);
    long prev_idx_shifted = r2000_uf_find(ft->prev_free, start); /* shifted space: real = value-1 */
    long prev_idx = prev_idx_shifted - 1L;

    long has_next = (next_idx < ft->count) ? 1L : 0L;
    long has_prev = (prev_idx >= 0L) ? 1L : 0L;

    if (!has_next && !has_prev)
        return 0L;

    if (has_next && has_prev)
    {
        unsigned long d_next = (by_loc[next_idx].location > target_loc) ?
                               (by_loc[next_idx].location - target_loc) : (target_loc - by_loc[next_idx].location);
        unsigned long d_prev = (by_loc[prev_idx].location > target_loc) ?
                               (by_loc[prev_idx].location - target_loc) : (target_loc - by_loc[prev_idx].location);
        *out_idx = (d_prev <= d_next) ? prev_idx : next_idx;
    }
    else if (has_next)
        *out_idx = next_idx;
    else
        *out_idx = prev_idx;

    return 1L;
}

/* First index in `by_loc` whose location is >= target_loc (standard
   binary-search insertion point) -- shared by every function below
   that needs a starting point for a nearest-location query. */
static long r2000_loc_insertion_point(const R2000_CARVE_SORT_ENTRY *by_loc, unsigned long count,
                                      unsigned long target_loc)
{
    long lo = 0L, hi = (long)count - 1L;
    while (lo < hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (by_loc[mid].location < target_loc)
            lo = mid + 1L;
        else
            hi = mid;
    }
    return lo;
}

static long r2000_nearest_by_location(const R2000_CARVE_SORT_ENTRY *by_loc, unsigned long count,
                                      unsigned long target_loc, unsigned char *claimed,
                                      R2000_FREE_TRACKER *free_tracker,
                                      unsigned long *out_handle, unsigned long *out_loc)
{
    long start, chosen;

    if (count == 0UL)
        return 0L;

    start = r2000_loc_insertion_point(by_loc, count, target_loc);

    if (!r2000_free_tracker_nearest(free_tracker, by_loc, start, target_loc, &chosen))
        return 0L; /* every candidate in the whole index is already claimed */

    claimed[by_loc[chosen].raw_index] = 1U;
    r2000_free_tracker_claim(free_tracker, chosen);
    *out_handle = by_loc[chosen].handle;
    *out_loc = by_loc[chosen].location;
    return 1L;
}

/* Simple array-based binary min-heap over (distance, entry, candidate)
   triples -- the priority queue r2000_refine_resync_assignments uses
   to process the GLOBALLY closest pending (uncertain entry, real
   object) pairing next, instead of whatever order entries happen to
   appear in the file. Standard "greedy edge, closest first" bipartite
   matching heuristic: provably better than arbitrary-order greedy
   (which is what inline resync alone amounts to -- the first entry to
   ask "what's nearby?" wins a candidate even if a LATER entry in file
   order would have been a far better, closer match for that same
   candidate). Uses lazy deletion: a popped (entry, candidate) pair is
   only trusted if the candidate is STILL free at pop time; if another,
   better-priority pop already claimed it, this entry's next-best
   candidate is recomputed and re-pushed instead. */
typedef struct
{
    unsigned long dist;
    long entry_idx;
    long by_loc_idx;
} R2000_REFINE_HEAP_ITEM;

typedef struct
{
    R2000_REFINE_HEAP_ITEM *items;
    unsigned long count;
    unsigned long capacity;
} R2000_REFINE_HEAP;

static R2000_REFINE_HEAP *r2000_refine_heap_create(unsigned long initial_capacity)
{
    R2000_REFINE_HEAP *h = (R2000_REFINE_HEAP *)malloc(sizeof(R2000_REFINE_HEAP));
    if (h == NULL) return NULL;
    if (initial_capacity < 16UL) initial_capacity = 16UL;
    h->items = (R2000_REFINE_HEAP_ITEM *)malloc((size_t)initial_capacity * sizeof(R2000_REFINE_HEAP_ITEM));
    if (h->items == NULL) { free(h); return NULL; }
    h->count = 0UL;
    h->capacity = initial_capacity;
    return h;
}

static void r2000_refine_heap_destroy(R2000_REFINE_HEAP *h)
{
    if (h == NULL) return;
    free(h->items);
    free(h);
}

static void r2000_refine_heap_push(R2000_REFINE_HEAP *h, unsigned long dist, long entry_idx, long by_loc_idx)
{
    unsigned long i;

    if (h->count >= h->capacity)
    {
        unsigned long new_cap = h->capacity * 2UL;
        R2000_REFINE_HEAP_ITEM *bigger = (R2000_REFINE_HEAP_ITEM *)realloc(h->items, (size_t)new_cap * sizeof(R2000_REFINE_HEAP_ITEM));
        if (bigger == NULL) return; /* drop this push rather than crash -- worst case, this entry keeps its phase-1 guess */
        h->items = bigger;
        h->capacity = new_cap;
    }

    i = h->count++;
    h->items[i].dist = dist;
    h->items[i].entry_idx = entry_idx;
    h->items[i].by_loc_idx = by_loc_idx;

    while (i > 0UL)
    {
        unsigned long parent = (i - 1UL) / 2UL;
        R2000_REFINE_HEAP_ITEM tmp;
        if (h->items[parent].dist <= h->items[i].dist)
            break;
        tmp = h->items[parent]; h->items[parent] = h->items[i]; h->items[i] = tmp;
        i = parent;
    }
}

static long r2000_refine_heap_pop(R2000_REFINE_HEAP *h, R2000_REFINE_HEAP_ITEM *out)
{
    unsigned long i;

    if (h->count == 0UL)
        return 0L;

    *out = h->items[0];
    h->count--;
    h->items[0] = h->items[h->count];

    i = 0UL;
    for (;;)
    {
        unsigned long left = 2UL * i + 1UL, right = 2UL * i + 2UL, smallest = i;
        if (left < h->count && h->items[left].dist < h->items[smallest].dist) smallest = left;
        if (right < h->count && h->items[right].dist < h->items[smallest].dist) smallest = right;
        if (smallest == i) break;
        { R2000_REFINE_HEAP_ITEM tmp = h->items[smallest]; h->items[smallest] = h->items[i]; h->items[i] = tmp; }
        i = smallest;
    }
    return 1L;
}

/* The global refinement pass itself: revisits every TIER-1 (location-
   resync) entry from the first pass and reassigns it using
   greedy-closest-first priority instead of file-order-first-come.
   HIGH-CONFIDENCE (tier 0) entries are never touched -- only their
   already-claimed candidates are protected from being stolen. */
static void r2000_refine_resync_assignments(DWG_R2000_OBJMAP *out, const unsigned char *tier,
                                            const unsigned long *resync_target_loc,
                                            const R2000_CARVE_SORT_ENTRY *by_loc, unsigned long candidate_count,
                                            const long *raw_to_loc_pos)
{
    unsigned char *claimed2;
    R2000_FREE_TRACKER *ft2;
    R2000_REFINE_HEAP *heap;
    unsigned long i, resync_count = 0UL;
    R2000_REFINE_HEAP_ITEM item;

    if (candidate_count == 0UL || by_loc == NULL || tier == NULL || resync_target_loc == NULL)
        return;

    claimed2 = (unsigned char *)calloc((size_t)candidate_count, 1UL);
    if (claimed2 == NULL)
        return;

    /* Step 1: protect every HIGH-CONFIDENCE entry's current candidate;
       RESYNC-tier entries' current (tentative, phase-1) candidates are
       simply never marked here, releasing them back into consideration. */
    for (i = 0UL; i < out->count; i++)
    {
        if (tier[i] == 0U)
        {
            long idx = r2000_loc_insertion_point(by_loc, candidate_count, out->entries[i].location);
            if (idx < (long)candidate_count && by_loc[idx].location == out->entries[i].location)
                claimed2[by_loc[idx].raw_index] = 1U;
        }
        else
        {
            resync_count++;
        }
    }

    if (resync_count == 0UL)
    {
        free(claimed2);
        return;
    }

    ft2 = r2000_free_tracker_create(candidate_count);
    if (ft2 == NULL)
    {
        free(claimed2);
        return;
    }
    for (i = 0UL; i < candidate_count; i++)
        if (claimed2[i] && raw_to_loc_pos != NULL)
            r2000_free_tracker_claim(ft2, raw_to_loc_pos[i]);

    heap = r2000_refine_heap_create(resync_count);
    if (heap == NULL)
    {
        free(claimed2);
        r2000_free_tracker_destroy(ft2);
        return;
    }

    for (i = 0UL; i < out->count; i++)
    {
        if (tier[i] != 0U)
        {
            long start = r2000_loc_insertion_point(by_loc, candidate_count, resync_target_loc[i]);
            long chosen;
            if (r2000_free_tracker_nearest(ft2, by_loc, start, resync_target_loc[i], &chosen))
            {
                unsigned long dist = (by_loc[chosen].location > resync_target_loc[i]) ?
                                     (by_loc[chosen].location - resync_target_loc[i]) :
                                     (resync_target_loc[i] - by_loc[chosen].location);
                /* peek only -- does NOT claim yet, unlike r2000_nearest_by_location */
                r2000_refine_heap_push(heap, dist, (long)i, chosen);
            }
        }
    }

    while (r2000_refine_heap_pop(heap, &item))
    {
        unsigned long raw_idx = (unsigned long)by_loc[item.by_loc_idx].raw_index;

        if (!claimed2[raw_idx])
        {
            claimed2[raw_idx] = 1U;
            r2000_free_tracker_claim(ft2, item.by_loc_idx);
            out->entries[item.entry_idx].handle = by_loc[item.by_loc_idx].handle;
            out->entries[item.entry_idx].location = by_loc[item.by_loc_idx].location;
        }
        else
        {
            /* stale -- this candidate was claimed by a higher-priority
               (closer) pop since we computed this entry's guess;
               recompute its next-best remaining option. */
            long start = r2000_loc_insertion_point(by_loc, candidate_count, resync_target_loc[item.entry_idx]);
            long chosen;
            if (r2000_free_tracker_nearest(ft2, by_loc, start, resync_target_loc[item.entry_idx], &chosen))
            {
                unsigned long dist = (by_loc[chosen].location > resync_target_loc[item.entry_idx]) ?
                                     (by_loc[chosen].location - resync_target_loc[item.entry_idx]) :
                                     (resync_target_loc[item.entry_idx] - by_loc[chosen].location);
                r2000_refine_heap_push(heap, dist, item.entry_idx, chosen);
            }
            /* else: no candidates left anywhere -- leave this entry's
               original phase-1 guess as final, better than nothing. */
        }
    }

    r2000_refine_heap_destroy(heap);
    r2000_free_tracker_destroy(ft2);
    free(claimed2);
}

/* The deepest remaining gap this investigation found (see the long
   comment on r2000_refine_resync_assignments' own result, and the
   session's memory notes): for badly-drifted entries, BYTE PROXIMITY
   in the file is simply not a reliable signal of where their true
   content lives -- confirmed directly (test_find_handle_candidates.c)
   that real, structurally-valid, geometrically-plausible candidates
   for genuinely missing content (a real PUERTAS door line) exist in
   the raw scan pool but are never "close enough" to any drifted
   entry's guess to get chosen by ANY matching algorithm working from
   that signal, however globally optimal.

   Real insight: an object-map ENTRY doesn't need to "own" a candidate
   for that candidate's content to be worth recovering. A candidate's
   OWN geometry, once decoded, is independently verifiable evidence of
   its own reality -- completely independent of which handle some
   broken accumulator thinks should be nearby. So instead of only ever
   asking "which entry does this candidate belong to", this pass asks
   a simpler, stronger question directly: "is this raw candidate's own
   geometry real?" -- and if yes, includes it in the document as its
   own entry (using its own real embedded handle), with NO reliance on
   file-position proximity, resync guesses, or matching quality at
   all. This is a fundamentally different, additional signal (the
   candidate's OWN decoded coordinates passing the same plausibility
   gate the real decoders already trust at final acceptance time), not
   a refinement of the location-proximity approach -- it recovers
   exactly the class of content (PUERTAS/COVERING/most of MUROS on
   `03_Planta 2 Alta_A3.dwg`) that byte-proximity-based matching, no
   matter how globally optimal, provably cannot reach. */
static int r2000_ul_cmp(const void *a, const void *b)
{
    unsigned long ua = *(const unsigned long *)a, ub = *(const unsigned long *)b;
    return (ua < ub) ? -1 : (ua > ub) ? 1 : 0;
}

static int r2000_ul_sorted_contains(const unsigned long *arr, unsigned long count, unsigned long value)
{
    long lo = 0L, hi = (long)count - 1L;
    while (lo <= hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (arr[mid] == value) return 1;
        if (arr[mid] < value) lo = mid + 1L; else hi = mid - 1L;
    }
    return 0;
}

typedef struct { unsigned long handle; unsigned long cand_idx; } R2000_SALVAGE_CAND;

static int r2000_salvage_cand_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const R2000_SALVAGE_CAND *)a)->handle;
    unsigned long hb = ((const R2000_SALVAGE_CAND *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

typedef struct { unsigned long handle; unsigned long entry_idx; } R2000_HANDLE_IDX;

static int r2000_handle_idx_cmp(const void *a, const void *b)
{
    unsigned long ha = ((const R2000_HANDLE_IDX *)a)->handle;
    unsigned long hb = ((const R2000_HANDLE_IDX *)b)->handle;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

/* Returns the entry index for `value` if present in the sorted array,
   or -1 if not found. */
static long r2000_handle_idx_find(const R2000_HANDLE_IDX *arr, unsigned long count, unsigned long value)
{
    long lo = 0L, hi = (long)count - 1L;
    while (lo <= hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (arr[mid].handle == value) return (long)arr[mid].entry_idx;
        if (arr[mid].handle < value) lo = mid + 1L; else hi = mid - 1L;
    }
    return -1L;
}

static void r2000_salvage_orphan_candidates(const unsigned char *data, unsigned long length,
                                            DWG_R2000_OBJMAP *out, const R2000_CARVE_ENTRY *raw_candidates,
                                            unsigned long candidate_count, const R2000_CARVE_SORT_ENTRY *by_loc,
                                            unsigned short max_class_type, long is_r1314,
                                            const unsigned char *tier)
{
    unsigned char *used;
    unsigned long i;
    DWG_R2000_OBJMAP_ENTRY *bigger;
    unsigned int old_fpu_cw;

    if (candidate_count == 0UL || raw_candidates == NULL || by_loc == NULL)
        return;

    /* Real, confirmed crash found via this exact addition: unlike
       every earlier geometry-confidence check this session (always
       run on a small, already handle-pre-filtered candidate subset),
       this salvage pass evaluates coordinate plausibility for
       essentially EVERY raw candidate in the whole file -- tens of
       thousands, the overwhelming majority genuine structural false
       positives, not real objects. Reading their "coordinates" as raw
       IEEE754 doubles (dwg_bs_read_rd/dd/bd, a plain memcpy of
       whatever bits happen to be there) can and does produce
       signaling-NaN bit patterns often enough at this scale to
       actually hit one -- and this compiler's/runtime's default FPU
       control word does NOT mask the "invalid operand" exception,
       so simply COMPARING such a value (`v > -X`) traps and crashes
       the whole process, not just this one candidate. Masking all FPU
       exceptions for the duration of this pass (restored afterward)
       is the standard, minimal fix -- garbage doubles just compare as
       ordinary (false) results instead of trapping, which is exactly
       the behavior is_plausible_coord already wants for a bad
       candidate anyway. */
    old_fpu_cw = _control87(MCW_EM, MCW_EM);

    used = (unsigned char *)calloc((size_t)candidate_count, 1UL);
    if (used == NULL)
    {
        _control87(old_fpu_cw, MCW_EM);
        return;
    }

    /* Mark every raw candidate ALREADY represented (by any entry,
       high-confidence or resync/refined) as used, via exact-location
       lookup in the by-location sorted view. */
    for (i = 0UL; i < out->count; i++)
    {
        long idx = r2000_loc_insertion_point(by_loc, candidate_count, out->entries[i].location);
        if (idx < (long)candidate_count && by_loc[idx].location == out->entries[i].location)
            used[by_loc[idx].raw_index] = 1U;
    }

    /* Real, confirmed regression found via this exact addition: a
       salvaged orphan's handle comes straight from its own embedded
       handle field, with NO check against handles ALREADY present in
       out->entries -- and small handle values are extremely common
       coincidental matches in a dense binary file (measured earlier
       this session: handle=19 alone had 2,313 raw candidates). Adding
       thousands of orphans with no collision check corrupted
       binary-search handle lookups (objmap_find, used for LAYER/STYLE/
       BLOCK_HEADER resolution) file-wide: R2000's unresolved-layer
       count jumped from 59/27,656 to 24,806/28,557 the moment orphan
       salvage landed, because a lookup for a real layer's handle could
       now land on an unrelated salvaged LINE/ARC/CIRCLE/POINT sharing
       that same handle number by pure chance instead of the real
       table record.

       Real, confirmed SECOND finding, found after Arturo reported the
       drawing "sigue incompleto" and a direct handle-existence check
       (test_missing_handle_diag.exe) proved real, structurally-valid,
       plausible-length candidates DO exist for handles the first fix
       was silently discarding: a blanket "skip any collision" is too
       blunt. Not every existing entry for a colliding handle is
       equally trustworthy -- `tier[idx]==0` (fast-path/exact-handle
       match) genuinely IS trustworthy and must stay protected, but
       `tier[idx]==1` (a location-only resync GUESS, possibly wrong by
       construction -- see r2000_nearest_by_location's own comment)
       is exactly the class of assignment a directly geometry-verified
       orphan should be allowed to CORRECT, not just avoid colliding
       with. Fix: look up each colliding handle's CURRENT entry; if
       tier==1, REPLACE that entry's location in place (upgrade a
       guess to a verified real object, no new entry needed); if
       tier==0, still skip (protect it, as before). */
    {
        R2000_HANDLE_IDX *existing_idx = (R2000_HANDLE_IDX *)malloc((size_t)out->count * sizeof(R2000_HANDLE_IDX));
        unsigned char *replaced_this_pass = (tier != NULL) ? (unsigned char *)calloc((size_t)out->count, 1UL) : NULL;

        if (existing_idx != NULL)
        {
            unsigned long j;
            for (j = 0UL; j < out->count; j++)
            {
                existing_idx[j].handle = out->entries[j].handle;
                existing_idx[j].entry_idx = j;
            }
            qsort(existing_idx, (size_t)out->count, sizeof(R2000_HANDLE_IDX), r2000_handle_idx_cmp);

            for (i = 0UL; i < candidate_count; i++)
            {
                long eidx;
                unsigned long h;

                if (used[i])
                    continue;

                eidx = r2000_handle_idx_find(existing_idx, out->count, raw_candidates[i].handle);
                if (eidx < 0L)
                    continue; /* no collision -- handled by the normal qualifying/append path below */

                if (tier == NULL || tier[eidx] == 0U || (replaced_this_pass != NULL && replaced_this_pass[eidx]))
                {
                    used[i] = 1U; /* protected high-confidence entry, or already replaced once this pass */
                    continue;
                }

                /* tier[eidx] == 1: low-confidence resync guess -- only
                   upgrade it if this orphan's OWN geometry actually
                   verifies (same gate every other salvage candidate
                   must pass); otherwise leave the existing guess alone
                   rather than replace it with something unverified. */
                if (is_r1314 ? r1314_orphan_geometry_ok(data, length, raw_candidates[i].location, max_class_type, &h)
                             : r2000_orphan_geometry_ok(data, length, raw_candidates[i].location, max_class_type, &h))
                {
                    out->entries[eidx].location = raw_candidates[i].location;
                    if (replaced_this_pass != NULL) replaced_this_pass[eidx] = 1U;
                }
                used[i] = 1U; /* either replaced or intentionally left alone -- not a fresh append either way */
            }
            free(existing_idx);
        }
        free(replaced_this_pass);
    }

    /* Real, confirmed THIRD finding, found the same way as the first
       two: even after excluding collisions against PRE-existing
       entries, orphan candidates can ALSO collide with EACH OTHER --
       the same small-handle-value coincidence (handle=19 alone had
       2,313 raw candidates on `03_Planta 2 Alta_A3.dwg`) applies just
       as much within the orphan pool itself. Adding several orphans
       that all happen to share one handle reintroduces the exact
       ambiguous-binary-search problem the collision check above was
       meant to solve, just among NEW entries instead of old ones.
       Fix: gather every qualifying orphan's (handle, candidate index)
       pair, sort by handle, and keep only the FIRST candidate in each
       handle group -- deterministic, and no worse than any other
       arbitrary tie-break, but at least self-consistent. */
    {
        R2000_SALVAGE_CAND *qualifying;
        unsigned long qcount = 0UL, kept_count;

        qualifying = (R2000_SALVAGE_CAND *)malloc((size_t)candidate_count * sizeof(R2000_SALVAGE_CAND));
        if (qualifying == NULL)
        {
            free(used);
            _control87(old_fpu_cw, MCW_EM);
            return;
        }

        for (i = 0UL; i < candidate_count; i++)
        {
            unsigned long h;
            if (used[i])
                continue;
            if (is_r1314 ? r1314_orphan_geometry_ok(data, length, raw_candidates[i].location, max_class_type, &h)
                         : r2000_orphan_geometry_ok(data, length, raw_candidates[i].location, max_class_type, &h))
            {
                qualifying[qcount].handle = h;
                qualifying[qcount].cand_idx = i;
                qcount++;
            }
        }

        if (qcount == 0UL)
        {
            free(qualifying);
            free(used);
            _control87(old_fpu_cw, MCW_EM);
            return;
        }

        qsort(qualifying, (size_t)qcount, sizeof(R2000_SALVAGE_CAND), r2000_salvage_cand_cmp);

        kept_count = 0UL;
        for (i = 0UL; i < qcount; i++)
        {
            if (i == 0UL || qualifying[i].handle != qualifying[i - 1UL].handle)
                kept_count++;
        }

        bigger = (DWG_R2000_OBJMAP_ENTRY *)realloc(out->entries,
                                                   (size_t)(out->count + kept_count) * sizeof(DWG_R2000_OBJMAP_ENTRY));
        if (bigger == NULL)
        {
            free(qualifying);
            free(used);
            _control87(old_fpu_cw, MCW_EM);
            return; /* keep what we already had rather than losing it */
        }
        out->entries = bigger;

        for (i = 0UL; i < qcount; i++)
        {
            if (i == 0UL || qualifying[i].handle != qualifying[i - 1UL].handle)
            {
                unsigned long cidx = qualifying[i].cand_idx;
                out->entries[out->count].handle = qualifying[i].handle;
                out->entries[out->count].location = raw_candidates[cidx].location;
                out->count++;
            }
        }

        free(qualifying);
    }

    free(used);
    _control87(old_fpu_cw, MCW_EM);
}

/* Real, confirmed bug: a handle value alone is FAR from a unique key
   in this index. Measured on `03_Planta 2 Alta_A3.dwg`: 68% of all
   90,809 carve candidates share their handle with at least one other
   candidate -- handle=19 alone had 2,313 "matches" scattered across
   the entire 4MB file (small handles need very few bits to encode, so
   accidentally landing on one is common in a dense binary file). A
   plain binary search just returns WHICHEVER duplicate it happens to
   land on -- silently attaching a real, structurally-valid-looking
   but WRONG object's geometry to the entry, which renders as
   plausible-looking content in the wrong place rather than obvious
   garbage (exactly the "elements out of position" Arturo reported
   from a real screenshot, not something a "does it look coherent"
   visual check alone can catch).

   Fix: when a handle has multiple candidates, don't guess arbitrarily
   -- pick the one whose location is CLOSEST to `reference_loc` (the
   delta-accumulator's own, possibly-drifted-but-usually-roughly-right
   estimate for where this entry belongs). This is the same insight
   behind the old, since-deleted windowed search (the true location is
   often "in the neighborhood" even when not exact), now applied only
   as a tie-breaker among ALREADY handle-verified candidates instead
   of as the primary (much weaker) search strategy.

   ALSO enforces the same claimed[] uniqueness r2000_nearest_by_
   location does (see that function's own comment for the real,
   measured regression this fixes -- 42% of the final object map
   pointing at an already-used location before this existed): only
   considers UNCLAIMED candidates among the handle's matches, and
   claims the winner before returning -- in BOTH the `claimed[]` flags
   AND the `free_tracker` union-find `r2000_nearest_by_location` uses
   (via `raw_to_loc_pos`, mapping a raw candidate index to its
   position in the BY-LOCATION sorted view, since this function itself
   only ever sees the BY-HANDLE view). Real, confirmed bug found and
   fixed here: without this, a handle-keyed claim only updated
   `claimed[]`, leaving `free_tracker` unaware -- so a later location-
   based resync could still "find" and reuse the SAME slot via the
   union-find structure, since union-find's own idea of "taken" only
   ever updated through r2000_free_tracker_claim, never through
   `claimed[]` directly. Two independent bookkeeping structures that
   don't agree is worse than either alone; this keeps them in lockstep
   regardless of which lookup path claims a given candidate first. */
static long r2000_carve_lookup(const R2000_CARVE_SORT_ENTRY *arr, unsigned long count,
                               unsigned long handle, unsigned long reference_loc,
                               unsigned char *claimed, const long *raw_to_loc_pos,
                               R2000_FREE_TRACKER *free_tracker, unsigned long *out_loc)
{
    long lo = 0L, hi = (long)count - 1L;
    long found = -1L;

    while (lo <= hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (arr[mid].handle == handle)
        {
            found = mid;
            break;
        }
        if (arr[mid].handle < handle)
            lo = mid + 1L;
        else
            hi = mid - 1L;
    }

    if (found < 0L)
        return 0L;

    {
        long first = found, last = found;
        long best = -1L;
        unsigned long best_dist = 0UL;

        while (first > 0L && arr[first - 1L].handle == handle)
            first--;
        while ((unsigned long)last + 1UL < count && arr[last + 1L].handle == handle)
            last++;

        for (; first <= last; first++)
        {
            unsigned long dist;

            if (claimed[arr[first].raw_index])
                continue;

            dist = (arr[first].location > reference_loc) ?
                  (arr[first].location - reference_loc) : (reference_loc - arr[first].location);
            if (best < 0L || dist < best_dist)
            {
                best_dist = dist;
                best = first;
            }
        }

        if (best < 0L)
            return 0L; /* every candidate for this handle is already claimed */

        claimed[arr[best].raw_index] = 1U;
        r2000_free_tracker_claim(free_tracker, raw_to_loc_pos[arr[best].raw_index]);
        *out_loc = arr[best].location;
        return 1L;
    }
}

/* Marks a candidate claimed via an EXACT location match, used for the
   fast-path case (last_loc already decodes last_handle correctly, no
   carve/resync needed) -- without this, a fast-path success's own
   location would stay unclaimed and remain eligible for some LATER,
   unrelated entry's carve/resync lookup to steal, reintroducing the
   same many-to-one collision this whole mechanism exists to prevent. */
static void r2000_claim_location_if_present(const R2000_CARVE_SORT_ENTRY *by_loc, unsigned long count,
                                             unsigned long location, unsigned char *claimed,
                                             R2000_FREE_TRACKER *free_tracker)
{
    long lo = 0L, hi = (long)count - 1L;

    while (lo <= hi)
    {
        long mid = lo + (hi - lo) / 2L;
        if (by_loc[mid].location == location)
        {
            claimed[by_loc[mid].raw_index] = 1U;
            r2000_free_tracker_claim(free_tracker, mid);
            return;
        }
        if (by_loc[mid].location < location)
            lo = mid + 1L;
        else
            hi = mid - 1L;
    }
}

static long dwg_r2000_parse_object_map_impl(const unsigned char *data, unsigned long length,
                                            unsigned long seeker, unsigned long size,
                                            DWG_R2000_OBJMAP *out, long is_r1314)
{
    unsigned long pos = seeker;
    unsigned long capacity = 0UL;
    unsigned long last_handle = 0UL;
    unsigned long last_loc = 0UL;
    R2000_CARVE_ENTRY *raw_candidates;
    R2000_CARVE_SORT_ENTRY *by_handle;
    R2000_CARVE_SORT_ENTRY *by_loc;
    unsigned char *claimed;
    R2000_FREE_TRACKER *free_tracker;
    long *raw_to_loc_pos;
    unsigned long candidate_count;
    unsigned short max_class_type;
    unsigned char *tier;
    unsigned long *resync_target_loc;

    out->entries = NULL;
    out->count = 0UL;

    if (data == NULL || length < seeker + size)
        return 0L;

    max_class_type = r2000_max_class_type(data, length);
    raw_candidates = r2000_build_raw_candidates(data, length, &candidate_count, max_class_type, is_r1314);
    by_handle = r2000_build_sorted_view(raw_candidates, candidate_count, 0);
    by_loc = r2000_build_sorted_view(raw_candidates, candidate_count, 1);
    raw_to_loc_pos = r2000_build_raw_to_loc_pos(by_loc, candidate_count);
    claimed = (candidate_count > 0UL) ? (unsigned char *)calloc((size_t)candidate_count, 1UL) : NULL;
    free_tracker = (candidate_count > 0UL) ? r2000_free_tracker_create(candidate_count) : NULL;

    /* Tracks, per objmap entry (in push order, same indexing as
       out->entries before the final handle-sort), whether it was a
       HIGH-CONFIDENCE assignment (fast path or exact handle match --
       trustworthy, left alone) or a LOW-CONFIDENCE one (location-only
       resync -- just a "nearest available object" guess). See
       r2000_refine_resync_assignments below for why this matters: a
       size upper bound of `size` (the object-map section's own byte
       length) is always safe since each entry needs at least 2 bytes
       of MC-encoded deltas. */
    {
        unsigned long tier_capacity = size + 1UL;
        tier = (unsigned char *)calloc((size_t)tier_capacity, 1UL);
        resync_target_loc = (unsigned long *)malloc((size_t)tier_capacity * sizeof(unsigned long));
    }

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
            long fast_ok, carve_ok = 0L;
            int is_resync_tier = 0;
            unsigned long pre_resync_loc_value = 0UL;

            last_handle += (unsigned long)hoff;
            last_loc = (unsigned long)((long)last_loc + loff);

            /* Fast path: the accumulator already landed correctly (the
               overwhelming majority of entries, even in a corrupted
               file -- most of the map is fine, only certain stretches
               drift). */
            fast_ok = is_r1314 ?
                     r1314_offset_decodes_to_handle_shared(data, length, last_loc, last_handle, max_class_type) :
                     r2000_offset_decodes_to_handle(data, length, last_loc, last_handle, max_class_type);

            if (fast_ok && claimed != NULL)
                r2000_claim_location_if_present(by_loc, candidate_count, last_loc, claimed, free_tracker);

            if (!fast_ok && by_handle != NULL && claimed != NULL && raw_to_loc_pos != NULL && free_tracker != NULL)
                carve_ok = r2000_carve_lookup(by_handle, candidate_count, last_handle, last_loc,
                                              claimed, raw_to_loc_pos, free_tracker, &carved_loc);

            if (!fast_ok && carve_ok)
            {
                last_loc = carved_loc;
            }
            else if (!fast_ok && !carve_ok)
            {
                /* Neither the accumulator's own position NOR a handle-
                   keyed lookup found a trustworthy match -- strong
                   evidence `last_handle` itself has drifted (measured:
                   76-86% of a real file's accumulated handles don't
                   exist anywhere in that file's own DXF export -- see
                   the long comment on r2000_nearest_by_location
                   above). Provisionally RESYNC both handle and
                   location from the nearest real object found by
                   LOCATION, so every entry after this one accumulates
                   from a corrected base rather than inheriting the
                   same drift indefinitely -- but mark this entry
                   TIER_RESYNC and record what position it was
                   originally aiming for, so r2000_refine_resync_
                   assignments (below, after this whole first pass
                   completes) can revisit it with a globally-aware
                   matching pass instead of trusting this greedy,
                   file-order-dependent first guess as final. */
                unsigned long resync_handle, resync_loc;
                is_resync_tier = 1;
                pre_resync_loc_value = last_loc;
                if (by_loc != NULL && claimed != NULL && free_tracker != NULL &&
                    r2000_nearest_by_location(by_loc, candidate_count, last_loc, claimed, free_tracker,
                                              &resync_handle, &resync_loc))
                {
                    last_handle = resync_handle;
                    last_loc = resync_loc;
                }
            }

            {
                unsigned long push_index = out->count;
                if (!objmap_push(out, last_handle, last_loc, &capacity))
                {
                    free(raw_candidates);
                    free(by_handle);
                    free(by_loc);
                    free(claimed);
                    free(raw_to_loc_pos);
                    free(tier);
                    free(resync_target_loc);
                    r2000_free_tracker_destroy(free_tracker);
                    dwg_r2000_objmap_free(out);
                    return 0L;
                }
                if (is_resync_tier && tier != NULL && push_index < size)
                {
                    tier[push_index] = 1U;
                    resync_target_loc[push_index] = pre_resync_loc_value;
                }
            }
        }

        pos = section_end_byte; /* skip the trailing 2-byte CRC (not validated here) */
        pos += 2UL;
    }

    r2000_refine_resync_assignments(out, tier, resync_target_loc, by_loc, candidate_count, raw_to_loc_pos);
    r2000_salvage_orphan_candidates(data, length, out, raw_candidates, candidate_count, by_loc, max_class_type, is_r1314, tier);

    free(raw_candidates);
    free(by_handle);
    free(by_loc);
    free(claimed);
    free(raw_to_loc_pos);
    free(tier);
    free(resync_target_loc);
    r2000_free_tracker_destroy(free_tracker);

    /* Real, confirmed regression from the resync mechanism above: hoff
       deltas are always non-negative, so PLAIN accumulation alone
       (the only thing this array's consumers were ever tested
       against) is naturally sorted ascending by handle -- several
       callers (objmap_find, in both dwg_r2000_entity_reader.c and
       dwg_r1314_entity_reader.c) rely on exactly that, doing a binary
       search over out->entries. A resync can jump `last_handle` to
       ANY real value found nearby (not necessarily >= the previous
       entry's handle), breaking that ordering wherever a resync fires
       -- measured impact: R2000 layer-name resolution collapsed from
       7/6,709 unresolved to 18,868/18,868 (100%) unresolved after the
       resync fix landed, purely from this ordering break, not from
       any actual loss of real handle/location data. Re-sorting the
       WHOLE array by handle once, here, after all entries (resynced
       or not) are collected, restores the invariant every binary-
       search consumer already assumes, regardless of how many resyncs
       fired during the walk. */
    qsort(out->entries, (size_t)out->count, sizeof(DWG_R2000_OBJMAP_ENTRY), r2000_objmap_entry_cmp);

    return 1L;
}

long dwg_r2000_parse_object_map(const unsigned char *data, unsigned long length,
                                unsigned long seeker, unsigned long size,
                                DWG_R2000_OBJMAP *out)
{
    return dwg_r2000_parse_object_map_impl(data, length, seeker, size, out, 0L);
}

long dwg_r1314_parse_object_map(const unsigned char *data, unsigned long length,
                                unsigned long seeker, unsigned long size,
                                DWG_R2000_OBJMAP *out)
{
    return dwg_r2000_parse_object_map_impl(data, length, seeker, size, out, 1L);
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
