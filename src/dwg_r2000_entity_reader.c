#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dwg_r2000_reader.h"
#include "dwg_bitstream.h"
#include "dwg_document.h"
#include "dwg_geometry.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_mtext.h"
#include "dwg_text.h"
#include "dwg_solid.h"
#include "dwg_insert.h"
#include "dwg_transform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Object type codes, R2000 fixed values -- see
   reverse/DWG_R2000_format_reference.md's object-type table. */
#define DWG_R2000_TYPE_TEXT       0x01
#define DWG_R2000_TYPE_INSERT     0x07
#define DWG_R2000_TYPE_VERTEX2D   0x0A
#define DWG_R2000_TYPE_VERTEX3D   0x0B
#define DWG_R2000_TYPE_POLYLINE2D 0x0F
#define DWG_R2000_TYPE_POLYLINE3D 0x10
#define DWG_R2000_TYPE_ARC        0x11
#define DWG_R2000_TYPE_CIRCLE     0x12
#define DWG_R2000_TYPE_LINE       0x13
#define DWG_R2000_TYPE_POINT      0x1B
#define DWG_R2000_TYPE_SOLID      0x1F
#define DWG_R2000_TYPE_MTEXT      0x2C

#define DWG_R2000_TYPE_BLOCK_HEADER 0x31
#define DWG_R2000_TYPE_LAYER  0x33
#define DWG_R2000_TYPE_STYLE  0x35 /* internal ODA name "SHAPEFILE" -- see the reference doc */
#define DWG_R2000_MAX_LAYER_NAME 256
#define DWG_R2000_MAX_STYLE_NAME 256
#define DWG_R2000_MAX_BLOCK_NAME 256

/* Safety bound on VERTEX chain walking, guarding against a corrupt/
   cyclic handle chain in a malformed file -- never expected to trigger
   on a well-formed one. */
#define DWG_R2000_MAX_VERTICES 100000UL

/* Same idea, for walking a BLOCK_HEADER's own entity-definition chain
   (see decode_and_transform_block_entity below) -- a real block's
   internal entity count is never anywhere near this. */
#define DWG_R2000_MAX_BLOCK_ENTITIES 100000UL

/* Safety bound on Numreactors (BL, 32-bit, no inherent limit) --
   real objects never have more than a handful of persistent reactors,
   but a stale/garbage object-map entry (see the R13/R14 reader's
   dwg_r1314_reader.h for the general phenomenon) can decode ANY 32-bit
   value here, and every caller that resolves LAYER/STYLE/etc. loops
   `numreactors` times reading a handle. Confirmed as a REAL hang, not
   theoretical: a garbage object on Arturo's own real R2000 file
   decoded a huge Numreactors value and hung the reader for minutes
   before this bound was added -- see the memory entry documenting how
   this was traced (binary search on iteration count via temporary
   stderr progress prints, isolating the exact object-map index that
   never returned). */
#define DWG_R2000_MAX_REACTORS 1000UL

typedef struct
{
    unsigned long obj_size_bits;
    unsigned long handle;
    unsigned short color;
    unsigned long entmode;
    unsigned long numreactors;
    unsigned long nolinks;
    unsigned long ltflags;
    unsigned long plotstyleflags;
} DWG_R2000_COMMON_ENTITY;

/*
 * Extended Entity Data (spec chapter 28): a chain of
 * |Length(BS)|Application handle(H)|raw data bytes[Length]| blocks,
 * terminating at a block whose Length is 0. The internal structure of
 * the data bytes (DXF-1000-group-coded items) isn't interpreted here
 * -- this engine doesn't model EED at all yet -- just skipped so
 * decoding can continue past it. Confirmed against two real objects
 * that both actually carry EED (an MTEXT and its STYLE): before this,
 * both were silently dropped by this reader entirely, which turns out
 * to be common in real files, not a rare edge case.
 */
static void skip_eed_blocks(DWG_BITSTREAM *bs, unsigned short first_length)
{
    unsigned short length = first_length;

    while (length != 0U)
    {
        unsigned char code;
        unsigned long value;
        unsigned short i;

        dwg_bs_read_handle(bs, &code, &value); /* application handle: not used yet */

        for (i = 0U; i < length; i++)
            (void)dwg_bs_read_bits(bs, 8UL);

        length = dwg_bs_read_bs(bs);
    }
}

/* Decodes the Common Entity Data fields (R2000+ column, see the
   reference doc) from right after the object's Type field through
   Lineweight, leaving bs positioned at the start of the entity-
   specific fields. Returns 0 (and leaves bs in an unusable position)
   if an embedded graphic is present -- not handled by this first
   pass, rare for simple drafted entities; caller should skip the
   object entirely in that case, same as an unmodeled type. EED, if
   present, is skipped via skip_eed_blocks rather than bailing out. */
static int read_common_entity_data(DWG_BITSTREAM *bs, DWG_R2000_COMMON_ENTITY *out)
{
    unsigned short eed_size;
    unsigned long preview_exists;
    unsigned char handle_code;
    double ltscale;
    unsigned short invisibility;

    out->obj_size_bits = dwg_bs_read_rl(bs);
    dwg_bs_read_handle(bs, &handle_code, &out->handle);

    eed_size = dwg_bs_read_bs(bs);
    if (eed_size != 0U)
        skip_eed_blocks(bs, eed_size);

    /* REAL bug found+fixed: this bit is `preview_exists`, a genuine
       per-entity cached preview image -- NOT a marker meaning "this
       entity is unreadable" the way bailing out here always treated
       it. Confirmed against LibreDWG's real common_entity_data.spec
       (SINCE R_13b1 block): for every version up through R_2007
       (which covers this reader's R2000/AC1015 target), the size
       field right after this bit is `FIELD_CAST(preview_size, RL,
       BLL, 92)` -- a PLAIN raw 32-bit RL on disk (only R_2010b+ uses
       the compressed BLL bit-varint form, a distinct, later
       convention -- see the R2004+ reader's own `preview_exists`
       fix, dwg_r2004_entity_reader.c, which correctly uses BLL
       because it targets AC1024+). Any entity carrying a preview --
       common for BIM/Revit-authored PROXY_ENTITY content especially,
       since non-aware readers need SOMETHING to show -- was being
       thrown away ENTIRELY by this function returning 0 the instant
       it saw the bit set, discarding real color/layer/handle
       metadata that comes right after the (skippable) preview bytes
       for no reason related to those fields actually being unreadable. */
    preview_exists = dwg_bs_read_bit(bs);
    if (preview_exists != 0UL)
    {
        unsigned long preview_size = dwg_bs_read_rl(bs);
        if (preview_size > 0x7FFFFFFFUL) /* defensive: RL is unsigned, a
                                             corrupt/negative-looking value
                                             would seek wildly out of range */
            return 0;
        dwg_bs_seek_bit(bs, dwg_bs_tell_bit(bs) + preview_size * 8UL);
    }

    out->entmode = dwg_bs_read_bb(bs);
    out->numreactors = dwg_bs_read_bl(bs);
    if (out->numreactors > DWG_R2000_MAX_REACTORS)
        out->numreactors = DWG_R2000_MAX_REACTORS; /* see the constant's own comment -- clamp, don't trust raw garbage */
    out->nolinks = dwg_bs_read_bit(bs);

    out->color = dwg_bs_read_bs(bs); /* R2000: plain ACI index BS, see the reference doc's CMC note */
    ltscale = dwg_bs_read_bd(bs);
    out->ltflags = dwg_bs_read_bb(bs);
    out->plotstyleflags = dwg_bs_read_bb(bs);
    invisibility = dwg_bs_read_bs(bs);
    (void)ltscale; (void)invisibility;

    (void)dwg_bs_read_rc(bs); /* lineweight: not modeled by this engine yet */

    return 1;
}

/*
 * Reads Common Entity Handle Data (see the reference doc) to find this
 * entity's LAYER handle -- the only one of these references this
 * engine currently resolves. Jumps to the handle stream via the
 * confirmed obj_size formula (bitpos right after MS Length + obj_size
 * = handles_start_bit) rather than trusting that the entity-specific
 * field decode above consumed exactly the right number of bits --
 * more robust, and independently confirmed correct against a real
 * LINE (handle stream: xdicobjhandle, prev/next entity NULL links,
 * then LAYER handle 16, totaling exactly the bits available).
 * Returns 1 and fills *layer_handle on success, 0 if eed/graphics had
 * already made the object unusable (shouldn't be reached in that case)
 * or the position is out of bounds.
 */
static int read_entity_layer_handle(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                    const DWG_R2000_COMMON_ENTITY *common,
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

    if (common->nolinks == 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* previous entity, not used yet */
        dwg_bs_read_handle(bs, &code, &value); /* next entity, not used yet */
    }

    dwg_bs_read_handle(bs, &code, layer_handle); /* LAYER: always present */

    return 1;
}

/* Same idea as read_entity_layer_handle, but continues past LAYER (and
   [LTYPE]/[PLOTSTYLE] if present) to reach the STYLE handle -- only
   TEXT/MTEXT/ATTRIB/ATTDEF/DIMENSION carry one; those are the only
   types that should ever call this. */
static int read_entity_style_handle(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                    const DWG_R2000_COMMON_ENTITY *common,
                                    unsigned long *style_handle)
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

    if (common->nolinks == 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* previous entity */
        dwg_bs_read_handle(bs, &code, &value); /* next entity */
    }

    dwg_bs_read_handle(bs, &code, &value); /* LAYER: not needed here */

    if (common->ltflags == 3UL)
        dwg_bs_read_handle(bs, &code, &value); /* LTYPE */
    if (common->plotstyleflags == 3UL)
        dwg_bs_read_handle(bs, &code, &value); /* PLOTSTYLE */

    dwg_bs_read_handle(bs, &code, style_handle); /* STYLE */

    return 1;
}

/* Same idea again, but for INSERT's BLOCK HEADER handle -- which sits
   right where STYLE would for TEXT/MTEXT (after LAYER/[LTYPE]/
   [PLOTSTYLE]), just for a different entity type and target object
   type. */
static int read_entity_block_header_handle(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                           const DWG_R2000_COMMON_ENTITY *common,
                                           unsigned long *block_header_handle)
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

    if (common->nolinks == 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* previous entity */
        dwg_bs_read_handle(bs, &code, &value); /* next entity */
    }

    dwg_bs_read_handle(bs, &code, &value); /* LAYER: not needed here */

    if (common->ltflags == 3UL)
        dwg_bs_read_handle(bs, &code, &value); /* LTYPE */
    if (common->plotstyleflags == 3UL)
        dwg_bs_read_handle(bs, &code, &value); /* PLOTSTYLE */

    dwg_bs_read_handle(bs, &code, block_header_handle); /* BLOCK HEADER */

    return 1;
}

/* Finds handle's file location in an already-parsed object map.
   Returns 1 and fills *location on success, 0 if not present.
   Binary search, not linear: dwg_r2000_parse_object_map's handle-offset
   (MC, never negative -- see the spec quote in
   reverse/DWG_R1314_format_reference.md) means objmap->entries[] is
   ALWAYS non-decreasing by handle, an invariant of the format itself,
   not an assumption -- confirmed necessary, not just an optimization,
   after a real hang: this was a plain linear scan, fine for the small
   R2000 sample files (~200 entries) this reader was originally
   validated against, but a real ~58000-entry object map (see
   dwg_r1314_reader.h's "stale entries" finding -- R2000 files are
   affected too, this isn't R13/R14-specific) combined with the O(n)
   scan called once per decoded entity's LAYER/STYLE lookup made this
   effectively O(n^2), which hung on Arturo's own real R2000 file
   (02_Planta 1 Baja_A3.dwg, AC1015, ~1.7MB) for well over a minute. */
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

/*
 * Decodes just enough of a table-record object (Common non-entity
 * object format: MS/Type/ObjSize/Handle/EED/Numreactors) to get its
 * name -- used for both LAYER (type 0x33) and STYLE (type 0x35, whose
 * internal ODA name is actually "SHAPEFILE" -- see the reference doc)
 * since both share this exact prefix shape before their own specific
 * fields begin. expected_type rejects anything else so a caller
 * resolving, say, a LAYER handle doesn't silently read a same-shaped
 * but semantically different object.
 *
 * Confirmed against real objects (a real LAYER, handle 16, name "0",
 * color 7; a real STYLE, handle 17, name "Standard") before trusting
 * this: the ODA spec's own worked hex example for LAYER decoded to
 * nonsense when tried in isolation, traced to what looks like a PDF
 * text-extraction artifact in that particular table, NOT a flaw in
 * this field order -- real files are what this was actually validated
 * against, see reverse/DWG_R2000_format_reference.md.
 *
 * Real-file finding worth flagging: both LAYER's and STYLE's Entry
 * name field declared length INCLUDES a trailing null byte (confirmed
 * via the obj_size/subsequent-field values coming out plausible only
 * with that byte counted). dwg_bs_read_t already null-terminates
 * buf regardless, so this doesn't need special-casing here; it just
 * means the returned length may be one more than the "real" name
 * length a caller would expect from the R12/TEXT convention.
 */
/*
 * out_color: if non-NULL AND expected_type is DWG_R2000_TYPE_LAYER,
 * filled with the LAYER's real color (BS color index, spec p.166-167,
 * section 20.4.54) -- read past the name and the 64-flag/xrefindex+1/
 * Xdep/Values flag fields (R2000+'s "Values BS" folds
 * frozen/on/frozen-in-new/locked/plotting/lineweight into one field,
 * replacing R13-R14's four separate B bits -- irrelevant to us either
 * way, just needs to be consumed to reach Color). Ignored (left
 * unmodified) for STYLE/BLOCK_HEADER, which don't have this field in
 * the same position -- callers resolving those pass NULL. Added after
 * a real, visible gap: every LAYER this reader ever created defaulted
 * to color 7 (this engine's own default, see dwg_entity_create) because
 * nothing decoded the file's REAL layer color -- Arturo's own viewer
 * screenshot showed the whole drawing in one flat color as a result.
 */
static int decode_table_record_name(const unsigned char *data, unsigned long length,
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

    (void)dwg_bs_read_rl(&bs); /* obj_size: not needed here, we read sequentially */
    dwg_bs_read_handle(&bs, &handle_code, &handle_value);

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks(&bs, eed_size);

    (void)dwg_bs_read_bl(&bs); /* numreactors */

    (void)dwg_bs_read_t(&bs, name_buf, (unsigned short)buf_size);

    if (out_color != NULL && expected_type == DWG_R2000_TYPE_LAYER)
    {
        (void)dwg_bs_read_bit(&bs);  /* 64-flag */
        (void)dwg_bs_read_bs(&bs);   /* xrefindex+1 */
        (void)dwg_bs_read_bit(&bs);  /* Xdep */
        (void)dwg_bs_read_bs(&bs);   /* R2000+ Values (frozen/on/frozen-in-new/locked/plotting/lineweight combined) */
        *out_color = dwg_bs_read_bs(&bs); /* Color: R15 and earlier is a plain BS index, see spec section 2.11 */
    }

    return 1;
}

static void apply_color(HENTITY e, unsigned short color)
{
    /* 256 = BYLAYER (this engine's own default, see dwg_entity_create),
       0 = BYBLOCK: neither is a real explicit color to set. */
    if (color != 0U && color != 256U)
        dwg_entity_put_color(e, color);
}

/* Coordinate sanity check: a real drawing's coordinates, in any real-
   world unit (mm/cm/m/inch/foot), never come remotely close to this --
   10 million is generous headroom, not a tight bound. Added after a
   real, visible bug found by Arturo: opening a real file whose object
   map has stale/garbage entries (same phenomenon documented in
   dwg_r1314_reader.h) put a small number of LINEs with astronomical
   coordinates (seen: 1e101, 1e87 -- clearly IEEE754 garbage from
   misinterpreted bytes, not real geometry) into the document. Unlike
   POLYLINE's vertex-handle==0 check, garbage floating-point coordinates
   don't have an "impossible value" the way a handle does -- ANY bit
   pattern is a technically valid double -- so this is a plausibility
   heuristic, not a hard invariant; a real drawing at an absurdly large
   scale could in principle exceed it, but no real file at any sane
   scale (even kilometers-in-millimeters) would. Without this, zoom-to-
   fit's own extents calculation gets dominated by the outlier(s),
   scaling the ENTIRE real drawing down to an invisible point -- this
   is the actual visible symptom Arturo reported (a single stray line
   on an otherwise blank gray viewport). */
#define DWG_R2000_MAX_PLAUSIBLE_COORD 1.0e7

static int is_plausible_coord(double v)
{
    return v > -DWG_R2000_MAX_PLAUSIBLE_COORD && v < DWG_R2000_MAX_PLAUSIBLE_COORD;
}

/* Same real, confirmed fix as dwg_r1314_entity_reader.c's identical
   is_plausible_text -- see that copy's own comment for the full
   story (Arturo caught a real "K{r0" rendering on a real floor plan:
   a TEXT/MTEXT entry whose object-map location got resynced to a
   WRONG-but-structurally-valid candidate, decoding whatever bytes
   were there as a string; coordinates alone don't catch this since
   they can still pass the loose plausibility bound). */
static int is_plausible_text(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int has_alnum = 0;
    int is_empty = 1;

    for (; *p != '\0'; p++)
    {
        unsigned char c = *p;
        is_empty = 0;

        if (c < 0x09U || (c > 0x0DU && c < 0x20U) || c == 0x7FU)
            return 0; /* raw control character -- never real text */

        if (c == '{' || c == '}' || c == '|' || c == '~' || c == '^' ||
            c == '\\' || c == '<' || c == '>' || c == '`')
            return 0; /* legitimate DWG text chars, but vanishingly rare here */

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            has_alnum = 1;
    }

    return is_empty ? 1 : has_alnum;
}

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

    (void)dwg_bs_read_bt(bs); /* thickness: not modeled by dwg_geometry yet */
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion); /* extrusion: not modeled yet either */
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

    /* Real, confirmed false positive found via this exact gap: a huge
       fake CIRCLE (center exactly (0,0,0), radius exactly 1.0, no DXF
       match, empty layer) rendered on top of a real floor plan
       (Arturo: "el circulo no existe"). BD's own bitstream encoding
       has a well-known 2-bit short form for the two special values
       0.0 and 1.0 -- a misaligned resync guess (accepted upstream on
       structural grounds alone, no geometry check) has a real chance
       of landing on that opcode for BOTH center and radius at once.
       Position plausibility alone can't catch it -- (0,0,0) and 1.0
       are each individually ordinary -- so this checks the specific
       "center is exactly the origin" signature instead: requiring
       radius to ALSO independently be checked (see is_plausible_coord
       above) makes this a two-field coincidence, essentially
       impossible for genuine authored content, deliberately scoped to
       CIRCLE/ARC only (not POINT/LINE, where a single field landing
       on the origin is much weaker evidence alone). */
    if (center.x == 0.0 && center.y == 0.0 && center.z == 0.0)
        return NULL;

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

    /* Real, confirmed hang found via this exact gap: center/radius were
       checked but start_angle_rad/end_angle_rad never were. A garbage-
       but-finite angle from a misaligned candidate (e.g. a resync/
       salvage guess landing a few bits off) sails through unvalidated,
       and draw_arc's own sweep normalization (dwg_render.c) then either
       loops effectively forever bringing a huge sweep back into range,
       or -- if it does escape -- feeds a segment count cast from a huge
       double, undefined behavior that pegged the CPU in an astronomical
       draw loop on a real file (Arturo: "se cuelga"). Reusing
       is_plausible_coord here is deliberate: the same bounded-range/
       NaN-safe check already trusted for coordinates applies just as
       well to "is this a real angle in radians". */
    if (!is_plausible_coord(center.x) || !is_plausible_coord(center.y) || !is_plausible_coord(center.z) ||
        !is_plausible_coord(radius) || !is_plausible_coord(start_angle_rad) || !is_plausible_coord(end_angle_rad))
        return NULL;

    /* Same real false-positive fixed in decode_circle's own copy of
       this check just above -- see its comment for the full
       reasoning. */
    if (center.x == 0.0 && center.y == 0.0 && center.z == 0.0)
        return NULL;

    /* file stores angles in radians (confirmed empirically, see
       reverse/DWG_R2000_format_reference.md); this engine's own API
       uses degrees throughout (dwg_file_io.h). */
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
    (void)dwg_bs_read_bd(bs); /* X-axis angle: no field to store it in, see the header comment */

    if (!is_plausible_coord(p.x) || !is_plausible_coord(p.y) || !is_plausible_coord(p.z))
        return NULL;

    return dwg_add_point(hDwg, p.x, p.y, p.z);
}

/*
 * Decodes SOLID (type 0x1F). Confirmed against all 3 real objects in
 * entities-2d.dwg (2 real triangles with p3==p4, the standard
 * degenerate case, and one real quad) -- bit count matched the
 * confirmed obj_size formula exactly for all three. First attempt at
 * this used RD (raw double) confused with BD (compressed bitdouble)
 * for the corner points -- caught by the same real-file cross-check
 * discipline (garbage coordinates, bit count off), but this one was a
 * genuine transcription slip on this session's own part, not a PDF
 * extraction artifact like LAYER/MTEXT's -- worth remembering both
 * kinds of mistake are possible and the real-file check catches
 * either one.
 *
 * Each corner is 2RD (x,y only) -- Z comes from the shared Elevation
 * field, same convention as TEXT/POLYLINE/VERTEX.
 */
static HENTITY decode_solid(HDWG hDwg, DWG_BITSTREAM *bs)
{
    double elevation;
    double x1, y1, x2, y2, x3, y3, x4, y4;
    HENTITY e;

    (void)dwg_bs_read_bt(bs); /* thickness: not modeled by dwg_solid yet */
    elevation = dwg_bs_read_bd(bs);
    x1 = dwg_bs_read_rd(bs); y1 = dwg_bs_read_rd(bs);
    x2 = dwg_bs_read_rd(bs); y2 = dwg_bs_read_rd(bs);
    x3 = dwg_bs_read_rd(bs); y3 = dwg_bs_read_rd(bs);
    x4 = dwg_bs_read_rd(bs); y4 = dwg_bs_read_rd(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion); /* not modeled by dwg_solid yet */
    }

    if (!is_plausible_coord(x1) || !is_plausible_coord(y1) || !is_plausible_coord(x2) || !is_plausible_coord(y2) ||
        !is_plausible_coord(x3) || !is_plausible_coord(y3) || !is_plausible_coord(x4) || !is_plausible_coord(y4) ||
        !is_plausible_coord(elevation))
        return NULL;

    e = dwg_add_solid(hDwg, x1, y1, elevation, x2, y2, elevation,
                      x3, y3, elevation, x4, y4, elevation);

    return e;
}

/*
 * Decodes a BLOCK_HEADER (type 0x31) far enough to get its base point
 * and its first/last-entity handle pair (spec p.163-165, section
 * 20.4.52) -- everything between (Entry name through Block scaling) is
 * read and discarded just to stay correctly positioned in the
 * bitstream, same "trust the framing" convention used throughout this
 * reader. Returns 0 if this isn't really a BLOCK_HEADER, or if it's an
 * xref/overlaid-xref block (spec: the first/last-entity handle pair
 * simply ISN'T PRESENT in that case -- an external file this reader
 * has no access to, not a bug to work around).
 */
static int decode_block_header(const unsigned char *data, unsigned long length,
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
    unsigned long is_anonymous, has_atts, is_xref, is_xref_overlaid;
    unsigned long own_handle;
    char name_buf[DWG_R2000_MAX_BLOCK_NAME];
    char xref_pname[DWG_R2000_MAX_BLOCK_NAME];
    char block_desc[DWG_R2000_MAX_BLOCK_NAME];
    unsigned long preview_size, j;
    unsigned char insert_count_byte;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_BLOCK_HEADER)
        return 0;

    (void)dwg_bs_read_rl(&bs); /* obj_size */
    dwg_bs_read_handle(&bs, &handle_code, &handle_value); /* code 0: this object's own absolute handle */
    own_handle = handle_value;

    eed_size = dwg_bs_read_bs(&bs);
    if (eed_size != 0U)
        skip_eed_blocks(&bs, eed_size);

    numreactors = dwg_bs_read_bl(&bs);
    if (numreactors > DWG_R2000_MAX_REACTORS)
        numreactors = DWG_R2000_MAX_REACTORS;

    (void)dwg_bs_read_t(&bs, name_buf, (unsigned short)sizeof(name_buf));

    (void)dwg_bs_read_bit(&bs); /* 64-flag */
    (void)dwg_bs_read_bs(&bs);  /* xrefindex+1 */
    (void)dwg_bs_read_bit(&bs); /* Xdep */
    is_anonymous = dwg_bs_read_bit(&bs);
    has_atts = dwg_bs_read_bit(&bs);
    is_xref = dwg_bs_read_bit(&bs);
    is_xref_overlaid = dwg_bs_read_bit(&bs);
    (void)is_anonymous; (void)has_atts;
    (void)dwg_bs_read_bit(&bs); /* R2000+: Loaded Bit */

    dwg_bs_read_3bd(&bs, base);
    (void)dwg_bs_read_t(&bs, xref_pname, (unsigned short)sizeof(xref_pname));

    do
    {
        insert_count_byte = (unsigned char)dwg_bs_read_rc(&bs);
    } while (insert_count_byte != 0U);

    (void)dwg_bs_read_t(&bs, block_desc, (unsigned short)sizeof(block_desc));

    preview_size = dwg_bs_read_bl(&bs);
    if (preview_size > (unsigned long)length) /* garbage guard, same spirit as DWG_R1314_MAX_PLAUSIBLE_LENGTH */
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
        return 0; /* first/last entity handles genuinely aren't stored in this case */

    dwg_bs_read_handle(&bs, &handle_code, &handle_value);
    *first_entity_handle = dwg_bs_resolve_handle(handle_code, handle_value, own_handle);
    dwg_bs_read_handle(&bs, &handle_code, &handle_value);
    *last_entity_handle = dwg_bs_resolve_handle(handle_code, handle_value, own_handle);

    return 1;
}

/*
 * Finds hEntity's own "next" handle in its Common Entity Handle Data --
 * same walk read_entity_layer_handle does (owner/reactors/xdic/[prev,
 * next]), just stopping one step earlier (before LAYER) since that's
 * all decode_and_transform_block_entity below needs to keep walking a
 * BLOCK_HEADER's entity chain. Nolinks==1 means the implicit default
 * (own handle + 1) applies, same convention already confirmed for
 * VERTEX chains (see decode_vertex2d_and_advance).
 */
static unsigned long find_entity_next_handle(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                             const DWG_R2000_COMMON_ENTITY *common,
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
    dwg_bs_read_handle(bs, &code, &value); /* previous entity */
    dwg_bs_read_handle(bs, &code, &value); /* next entity -- the one we want */

    return dwg_bs_resolve_handle(code, value, cur_handle);
}

/*
 * Decodes one entity from inside a BLOCK's own definition (LINE/CIRCLE/
 * ARC/POINT/SOLID only -- the common shapes real "accesorios"/fixture
 * blocks use, e.g. door/window symbols; POLYLINE/TEXT/nested-INSERT
 * inside a block are a real, documented scope gap, not modeled yet),
 * applies the insert's transform, and returns its "next" handle so the
 * caller can keep walking the block's chain. Transform order (base
 * point -> origin, scale, rotate, move to insertion point) matches the
 * order already confirmed for dwg_entity_explode's own INSERT handling
 * (see dwg_transform.h) -- same standard block-reference transform,
 * applied here at READ time instead of on-demand explode, since this
 * engine's document model has no block-instancing concept for the
 * renderer to resolve later (see dwg_render.h's INSERT-is-skipped
 * note). Returns 0 (nothing added, *next_handle left unset) if this
 * isn't one of the modeled types, or EED/graphics made it unusable.
 */
static int decode_and_transform_block_entity(HDWG hDwg, const unsigned char *data, unsigned long length,
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
    DWG_R2000_COMMON_ENTITY common;
    HENTITY e = NULL;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    ms_end_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);

    if (obj_type != DWG_R2000_TYPE_LINE && obj_type != DWG_R2000_TYPE_CIRCLE &&
        obj_type != DWG_R2000_TYPE_ARC && obj_type != DWG_R2000_TYPE_POINT &&
        obj_type != DWG_R2000_TYPE_SOLID)
        return 0;

    if (!read_common_entity_data(&bs, &common))
        return 0;

    switch (obj_type)
    {
    case DWG_R2000_TYPE_LINE:   e = decode_line(hDwg, &bs);   break;
    case DWG_R2000_TYPE_CIRCLE: e = decode_circle(hDwg, &bs); break;
    case DWG_R2000_TYPE_ARC:    e = decode_arc(hDwg, &bs);    break;
    case DWG_R2000_TYPE_POINT:  e = decode_point(hDwg, &bs);  break;
    case DWG_R2000_TYPE_SOLID:  e = decode_solid(hDwg, &bs);  break;
    default: break;
    }

    *next_handle = find_entity_next_handle(&bs, ms_end_bit, &common, cur_handle);

    if (e == NULL)
        return 1; /* still a valid chain step (garbage geometry filtered by is_plausible_coord), just nothing to transform */

    apply_color(e, common.color);

    dwg_entity_move(e, -block_base->x, -block_base->y, -block_base->z);
    dwg_entity_scale_xyz(e, 0.0, 0.0, 0.0, scale_x, scale_y, scale_z);
    dwg_entity_rotate(e, 0.0, 0.0, 0.0, rotation_deg);
    dwg_entity_move(e, ins_x, ins_y, ins_z);

    return 1;
}

/*
 * Decodes INSERT (type 7). Confirmed against both real INSERTs in
 * entities-2d.dwg -- bit count matched the confirmed obj_size formula
 * exactly on the first attempt for both, including the one with
 * has_attribs=1 (whose first/last-ATTRIB handles are read but not
 * used -- ATTRIB isn't modeled by this engine yet).
 *
 * Unlike LAYER/STYLE resolution (applied centrally by the caller,
 * after the entity already exists), dwg_add_insert needs the block
 * name at CONSTRUCTION time -- so this function does its own handle-
 * stream walk to find and resolve the BLOCK HEADER handle before
 * calling dwg_add_insert, using the same decode_table_record_name
 * already validated for LAYER/STYLE (BLOCK HEADER shares the same
 * Common non-entity object format prefix). Confirmed against the real
 * file's own block names -- "BLOCK1"/"BLOCK2", the same well-known
 * LibreDWG fixture names already seen validating the R12 reader.
 *
 * Extrusion here is a real 3BD (like MTEXT, unlike LINE/CIRCLE/ARC/
 * POINT/SOLID/POLYLINE's compressed BE form) -- confirmed by the spec
 * table and the real file decoding correctly.
 */
static HENTITY decode_insert(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                             const DWG_R2000_COMMON_ENTITY *common,
                             const unsigned char *data, unsigned long length,
                             const DWG_R2000_OBJMAP *objmap)
{
    DWG_POINT3D ins, extrusion, block_base_pt;
    unsigned long dataflags;
    double sx, sy, sz, rotation_rad;
    unsigned long block_header_handle, block_header_loc;
    unsigned long first_block_entity, last_block_entity;
    int block_header_found;
    char block_name[DWG_R2000_MAX_BLOCK_NAME];
    HENTITY e;

    dwg_bs_read_3bd(bs, &ins);

    dataflags = dwg_bs_read_bb(bs);
    switch (dataflags)
    {
    case 3UL: /* scale is (1,1,1), no data */
        sx = 1.0; sy = 1.0; sz = 1.0;
        break;
    case 1UL: /* sx=1.0 fixed; sy,sz each DD with default 1.0 */
        sx = 1.0;
        sy = dwg_bs_read_dd(bs, 1.0);
        sz = dwg_bs_read_dd(bs, 1.0);
        break;
    case 2UL: /* sx=RD; sy=sz=sx */
        sx = dwg_bs_read_rd(bs);
        sy = sx;
        sz = sx;
        break;
    default: /* 0: sx=RD; sy,sz each DD with default sx */
        sx = dwg_bs_read_rd(bs);
        sy = dwg_bs_read_dd(bs, sx);
        sz = dwg_bs_read_dd(bs, sx);
        break;
    }

    rotation_rad = dwg_bs_read_bd(bs);
    dwg_bs_read_3bd(bs, &extrusion); /* not modeled by dwg_insert yet */
    (void)dwg_bs_read_bit(bs); /* has ATTRIBs: only used below to know whether to skip their handles */

    block_name[0] = '\0';
    block_header_handle = 0UL;
    block_header_loc = 0UL;
    block_header_found = (read_entity_block_header_handle(bs, ms_end_bit, common, &block_header_handle) &&
                          objmap_find(objmap, block_header_handle, &block_header_loc));
    if (block_header_found)
    {
        decode_table_record_name(data, length, block_header_loc, DWG_R2000_TYPE_BLOCK_HEADER,
                                 block_name, sizeof(block_name), NULL);
    }

    if (!is_plausible_coord(ins.x) || !is_plausible_coord(ins.y) || !is_plausible_coord(ins.z) ||
        !is_plausible_coord(sx) || !is_plausible_coord(sy) || !is_plausible_coord(sz))
        return NULL;

    /* Real "accesorios" (doors, windows, furniture symbols) live as the
       referenced block's own entities, not as anything the renderer can
       draw from the INSERT itself (see dwg_render.h -- INSERT has no
       block-instancing model to resolve at draw time). Explode at READ
       time instead: walk the block's first/last-entity chain (same
       mechanism as a POLYLINE's VERTEX chain) and append transformed
       copies of each entity straight into hDwg. */
    if (block_header_found &&
        decode_block_header(data, length, block_header_loc, &block_base_pt, &first_block_entity, &last_block_entity))
    {
        unsigned long cur = first_block_entity;
        unsigned long count;

        for (count = 0UL; count < DWG_R2000_MAX_BLOCK_ENTITIES; count++)
        {
            unsigned long eloc, next;
            int reached_last = (cur == last_block_entity);

            if (!objmap_find(objmap, cur, &eloc))
                break;
            if (eloc + 6UL >= length)
                break;

            if (!decode_and_transform_block_entity(hDwg, data, length, eloc, cur, &block_base_pt,
                                                   ins.x, ins.y, ins.z, sx, sy, sz,
                                                   rotation_rad * 180.0 / M_PI, &next))
                break;

            if (reached_last)
                break;

            cur = next;
        }
    }

    /* file stores angles in radians; this engine's API uses degrees. */
    e = dwg_add_insert(hDwg, block_name, ins.x, ins.y, ins.z, rotation_rad * 180.0 / M_PI);
    if (e != NULL)
        dwg_insert_set_scale(e, sx, sy, sz);

    return e;
}

/*
 * Decodes MTEXT (type 0x2C). Field order confirmed against a real
 * file (Text.dwg, LibreDWG's public test-data): the ODA spec's own
 * table shows an "H 7 STYLE (hard pointer)" line sitting between the
 * Text field and the R2000+ Linespacing fields, which looked at first
 * like the STYLE handle being read inline mid-object -- but decoding
 * that way gave garbage linespacing values and the bit count didn't
 * match the confirmed obj_size formula. Removing that inline handle
 * read (STYLE actually lives in the normal handle stream, after
 * LAYER, same as every other entity that carries one) made the text
 * decode to a real, legible string ("This a multiline text to check
 * if libredwg is able to read this in the current file format
 * version") and the bit count matched exactly -- same kind of
 * PDF-table-extraction artifact already seen once with LAYER, not a
 * real field. See reverse/DWG_R2000_format_reference.md.
 *
 * The real sample's own MTEXT (and its STYLE table record) both
 * carried genuine Extended Entity Data too -- this is what motivated
 * adding skip_eed_blocks in the first place, not a hypothetical case.
 *
 * Extrusion here is a real 3BD (unlike LINE/CIRCLE/ARC/POINT/POLYLINE,
 * which all use the compressed BE form) -- confirmed by the spec
 * table and consistent with the real file decoding correctly.
 */
static HENTITY decode_mtext(HDWG hDwg, DWG_BITSTREAM *bs)
{
    DWG_POINT3D insertion, extrusion, xaxis;
    double rect_width, text_height;
    unsigned short attachment;
    char text_buf[DWG_MTEXT_MAX];
    HENTITY e;

    dwg_bs_read_3bd(bs, &insertion);
    dwg_bs_read_3bd(bs, &extrusion); /* not modeled by dwg_mtext yet */
    dwg_bs_read_3bd(bs, &xaxis);     /* not modeled by dwg_mtext yet (no angle field derived from it) */
    rect_width = dwg_bs_read_bd(bs);
    text_height = dwg_bs_read_bd(bs);
    attachment = dwg_bs_read_bs(bs);
    (void)dwg_bs_read_bs(bs); /* drawing dir: not modeled yet */
    (void)dwg_bs_read_bd(bs); /* extents height: undocumented, not modeled */
    (void)dwg_bs_read_bd(bs); /* extents width: undocumented, not modeled */

    (void)dwg_bs_read_t(bs, text_buf, (unsigned short)sizeof(text_buf));

    if (!is_plausible_coord(insertion.x) || !is_plausible_coord(insertion.y) || !is_plausible_coord(insertion.z) ||
        !is_plausible_coord(rect_width) || !is_plausible_coord(text_height) || !is_plausible_text(text_buf))
        return NULL;

    e = dwg_add_mtext(hDwg, insertion.x, insertion.y, insertion.z, text_height, rect_width, text_buf);

    {
        double linespacing_factor;

        (void)dwg_bs_read_bs(bs); /* linespacing style: not modeled by dwg_mtext yet */
        linespacing_factor = dwg_bs_read_bd(bs);
        (void)dwg_bs_read_bit(bs); /* unknown bit: undocumented */

        if (e != NULL)
        {
            dwg_mtext_set_attach(e, attachment);
            dwg_mtext_set_line_space(e, linespacing_factor);
        }
    }

    return e;
}

/*
 * Decodes TEXT (type 1). Confirmed field-by-field against a real
 * object (entities-2d.dwg, text_value "FOO" -- same well-known test
 * string already seen in the R12 work, apparently the same LibreDWG
 * fixture data replicated across format versions) -- bit count matched
 * the confirmed obj_size formula exactly on the first attempt, unlike
 * MTEXT's inline-handle surprise: TEXT's own STYLE handle really does
 * live in the ordinary end-of-object handle stream, matching a literal
 * reading of the spec table for this entity (the PDF-extraction
 * artifact seen twice so far -- LAYER, MTEXT -- isn't universal).
 *
 * DataFlags (RC) gates which of the remaining fields are actually
 * present -- see reverse/DWG_R2000_format_reference.md's TEXT
 * prescription for the exact bit meanings. Insertion point is only
 * 2RD (x,y); Z comes from the shared Elevation field, same convention
 * already relied on for POLYLINE/VERTEX.
 */
static HENTITY decode_text(HDWG hDwg, DWG_BITSTREAM *bs)
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
        dwg_bs_read_be(bs, &extrusion); /* not modeled by dwg_text yet */
    }
    (void)dwg_bs_read_bt(bs); /* thickness: not modeled by dwg_text yet */

    if (!(dataflags & 0x04U))
        oblique = dwg_bs_read_rd(bs);
    if (!(dataflags & 0x08U))
        rotation = dwg_bs_read_rd(bs);

    height = dwg_bs_read_rd(bs);

    if (!(dataflags & 0x10U))
        width_factor = dwg_bs_read_rd(bs);

    (void)dwg_bs_read_t(bs, text_buf, (unsigned short)sizeof(text_buf));

    if (!(dataflags & 0x20U))
        (void)dwg_bs_read_bs(bs); /* generation: not modeled by dwg_text yet */
    if (!(dataflags & 0x40U))
        horiz = dwg_bs_read_bs(bs);
    if (!(dataflags & 0x80U))
        (void)dwg_bs_read_bs(bs); /* vertical align: not modeled by dwg_text yet */

    if (!is_plausible_coord(ix) || !is_plausible_coord(iy) || !is_plausible_coord(elevation) ||
        !is_plausible_coord(height) || !is_plausible_text(text_buf))
        return NULL;

    /* file stores angles in radians; this engine's API uses degrees
       (dwg_file_io.h), confirmed for ARC and reused here. */
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

/* Skips Common Entity Handle Data's owner/reactors/xdic/[prev,next]
   block (shared by every entity type), leaving bs positioned right at
   the LAYER handle -- same logic as read_entity_layer_handle, but
   exposed as its own step so decode_polyline2d can continue reading
   past LAYER (and [LTYPE]/[PLOTSTYLE]) to reach its own
   first/last-VERTEX handles. */
static void skip_to_layer_handle(DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                 const DWG_R2000_COMMON_ENTITY *common)
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

    if (common->nolinks == 0UL)
    {
        dwg_bs_read_handle(bs, &code, &value); /* previous entity */
        dwg_bs_read_handle(bs, &code, &value); /* next entity */
    }
}

/* Decodes just enough of a VERTEX (2D) (type 0x0A) to append it to
   pl, and returns that vertex's own "next" handle (resolved to an
   absolute value) so the caller can continue walking the chain.
   Returns 0 (nothing added, *next_handle left unset) if this isn't
   actually a VERTEX2D, has EED/graphics, or has no "next" link stored
   (nolinks!=0 -- the chain's real end, distinct from simply reaching
   last_vertex_handle, which the caller checks separately). elevation
   comes from the owning POLYLINE (2D VERTEX's own Z is always 0, see
   the reference doc). */
static int decode_vertex2d_and_advance(const unsigned char *data, unsigned long length,
                                       unsigned long loc, unsigned long cur_handle,
                                       HPOLYLINE pl, double elevation,
                                       unsigned long *next_handle)
{
    DWG_BITSTREAM bs;
    unsigned long ms_end_bit;
    unsigned short obj_type;
    DWG_R2000_COMMON_ENTITY common;
    double x, y, start_width, end_width, bulge, raw_start_width;
    unsigned char code;
    unsigned long value, i;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    ms_end_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_VERTEX2D)
        return 0;

    if (!read_common_entity_data(&bs, &common))
        return 0;

    (void)dwg_bs_read_rc(&bs); /* per-vertex flags: not modeled by dwg_vertex yet */
    {
        DWG_POINT3D p;
        dwg_bs_read_3bd(&bs, &p);
        x = p.x;
        y = p.y;
    }

    raw_start_width = dwg_bs_read_bd(&bs);
    if (raw_start_width < 0.0)
    {
        /* negative start width is a compression trick: both widths are
           abs(start width), and no separate end width is stored */
        end_width = -raw_start_width;
        start_width = end_width;
    }
    else
    {
        start_width = raw_start_width;
        end_width = dwg_bs_read_bd(&bs);
    }
    bulge = dwg_bs_read_bd(&bs);
    (void)dwg_bs_read_bd(&bs); /* tangent direction: not modeled by dwg_vertex yet */

    if (!is_plausible_coord(x) || !is_plausible_coord(y) || !is_plausible_coord(elevation))
        return 0; /* garbage vertex -- stop walking this chain rather than adding it */

    dwg_polyline_add_vertex2(pl, x, y, elevation, bulge, start_width, end_width);

    /* Nolinks==1 means "prev/next are BOTH the implicit default (own
       handle -1/+1)" -- the links exist, they're just not physically
       stored in the file. Confirmed against a real chain (a 3D
       POLYLINE's middle vertices all had nolinks=1, and treating that
       as "chain ends here" -- the original assumption -- silently
       dropped 4 of 6 real vertices; the fix, walking own_handle+1
       instead, reproduced the whole chain exactly). nolinks==0 means
       the real prev/next handles ARE stored, read explicitly below. */
    if (common.nolinks != 0UL)
    {
        *next_handle = cur_handle + 1UL;
        return 1;
    }

    /* jump to the handle stream and read owner/reactors/xdic/prev
       (discarded) then next (the one we need), same confirmed
       obj_size-based seek used everywhere else in this reader */
    dwg_bs_seek_bit(&bs, ms_end_bit + common.obj_size_bits);

    if (common.entmode == 0UL)
        dwg_bs_read_handle(&bs, &code, &value);

    for (i = 0UL; i < common.numreactors; i++)
        dwg_bs_read_handle(&bs, &code, &value);

    dwg_bs_read_handle(&bs, &code, &value); /* xdicobjhandle */
    dwg_bs_read_handle(&bs, &code, &value); /* previous entity */
    dwg_bs_read_handle(&bs, &code, &value); /* next entity -- the one we want */

    *next_handle = dwg_bs_resolve_handle(code, value, cur_handle);

    return 1;
}

/*
 * Decodes a 2D POLYLINE (type 0x0F): its own fields (flags/elevation/
 * extrusion/etc.), then walks its VERTEX chain via
 * decode_vertex2d_and_advance, from first_vertex_handle to
 * last_vertex_handle (both read from the handle stream, right after
 * LAYER/[LTYPE]/[PLOTSTYLE] -- confirmed against a real file: a
 * POLYLINE's first/last VERTEX handles, and each VERTEX's own "next"
 * link -- which uses the *relative* 0x6/0x8 handle codes
 * dwg_bs_resolve_handle exists for -- all resolved to the expected
 * real handles end to end, see reverse/DWG_R2000_format_reference.md).
 * bs is already positioned at the entity-specific fields (right after
 * Common Entity Data) when this is called, same convention as
 * decode_line/circle/arc/point.
 */
static HENTITY decode_polyline2d(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                 const DWG_R2000_COMMON_ENTITY *common,
                                 const unsigned char *data, unsigned long length,
                                 const DWG_R2000_OBJMAP *objmap)
{
    unsigned short flags;
    double elevation;
    HENTITY e;
    HPOLYLINE pl;

    flags = dwg_bs_read_bs(bs);
    (void)dwg_bs_read_bs(bs); /* curve type: not modeled by dwg_polyline yet */
    (void)dwg_bs_read_bd(bs); /* default start width: not modeled (per-vertex widths are) */
    (void)dwg_bs_read_bd(bs); /* default end width */
    (void)dwg_bs_read_bt(bs); /* thickness: not modeled yet */
    elevation = dwg_bs_read_bd(bs);
    {
        DWG_POINT3D extrusion;
        dwg_bs_read_be(bs, &extrusion); /* not modeled yet */
    }

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

        skip_to_layer_handle(bs, ms_end_bit, common);
        dwg_bs_read_handle(bs, &code, &value); /* LAYER: already applied centrally by the caller */

        if (common->ltflags == 3UL)
            dwg_bs_read_handle(bs, &code, &value);
        if (common->plotstyleflags == 3UL)
            dwg_bs_read_handle(bs, &code, &value);

        dwg_bs_read_handle(bs, &code, &value);
        first_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);
        dwg_bs_read_handle(bs, &code, &value);
        last_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);

        cur_handle = first_vertex_handle;
        for (count = 0UL; count < DWG_R2000_MAX_VERTICES; count++)
        {
            unsigned long vloc, next_handle;
            int reached_last = (cur_handle == last_vertex_handle);

            if (!objmap_find(objmap, cur_handle, &vloc))
                break; /* broken chain: keep whatever vertices were already added */

            if (!decode_vertex2d_and_advance(data, length, vloc, cur_handle, pl, elevation, &next_handle))
                break; /* not a VERTEX2D, EED/graphics present, or chain ends here (no next link) */

            if (reached_last)
                break; /* done: last_vertex_handle's own vertex was just added */

            cur_handle = next_handle;
        }
    }

    return e;
}

/*
 * VERTEX(3D) (type 0x0B): a much simpler sibling of VERTEX(2D) --
 * just per-vertex flags (RC, not modeled) and a real 3D point (unlike
 * VERTEX(2D), which always has Z==0 and relies on the owning
 * POLYLINE's shared Elevation instead -- 3D POLYLINE has no Elevation
 * field at all, each vertex carries its own real Z). No bulge/width/
 * tangent fields exist for this type. Confirmed against 2 real
 * VERTEX(3D) objects in reverse/samples/2000/PolyLine3D.dwg (bit count
 * matched the confirmed obj_size formula exactly for both).
 */
static int decode_vertex3d_and_advance(const unsigned char *data, unsigned long length,
                                       unsigned long loc, unsigned long cur_handle,
                                       HPOLYLINE pl, unsigned long *next_handle)
{
    DWG_BITSTREAM bs;
    unsigned long ms_end_bit;
    unsigned short obj_type;
    DWG_R2000_COMMON_ENTITY common;
    DWG_POINT3D p;
    unsigned char code;
    unsigned long value, i;

    dwg_bs_init(&bs, data, length);
    dwg_bs_seek_bit(&bs, loc * 8UL);

    (void)dwg_bs_read_ms(&bs);
    ms_end_bit = dwg_bs_tell_bit(&bs);
    obj_type = dwg_bs_read_bs(&bs);
    if (obj_type != DWG_R2000_TYPE_VERTEX3D)
        return 0;

    if (!read_common_entity_data(&bs, &common))
        return 0;

    (void)dwg_bs_read_rc(&bs); /* per-vertex flags: not modeled by dwg_vertex yet */
    dwg_bs_read_3bd(&bs, &p);

    if (!is_plausible_coord(p.x) || !is_plausible_coord(p.y) || !is_plausible_coord(p.z))
        return 0; /* garbage vertex -- stop walking this chain rather than adding it */

    dwg_polyline_add_vertex2(pl, p.x, p.y, p.z, 0.0, 0.0, 0.0); /* no bulge/width concept in 3D */

    /* Nolinks==1: implicit next = own handle + 1, not stored -- see
       the identical, more detailed comment in
       decode_vertex2d_and_advance (this is where the bug was actually
       found: 4 of this real file's 6 VERTEX(3D) objects have
       nolinks==1). */
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
    dwg_bs_read_handle(&bs, &code, &value); /* previous entity */
    dwg_bs_read_handle(&bs, &code, &value); /* next entity -- the one we want */

    *next_handle = dwg_bs_resolve_handle(code, value, cur_handle);

    return 1;
}

/*
 * Decodes a 3D POLYLINE (type 0x10): unlike its 2D sibling, this one
 * has TWO separate RC "Flags" fields (confirmed against a real object,
 * both 0 in that case -- no splined/closed flags set) rather than one
 * BS Flags + BS Curve type. This engine only models "closed" (bit 0
 * of the SECOND flags byte, per the spec's own annotation -- the first
 * byte is splined-related, not modeled here). No Elevation field
 * exists for 3D POLYLINE at all (each VERTEX carries its own real Z).
 * Otherwise identical structure to decode_polyline2d: first/last
 * VERTEX handles in the handle stream, each VERTEX's own relative
 * "next" link walked via decode_vertex3d_and_advance.
 */
static HENTITY decode_polyline3d(HDWG hDwg, DWG_BITSTREAM *bs, unsigned long ms_end_bit,
                                 const DWG_R2000_COMMON_ENTITY *common,
                                 const unsigned char *data, unsigned long length,
                                 const DWG_R2000_OBJMAP *objmap)
{
    unsigned char flags2;
    HENTITY e;
    HPOLYLINE pl;

    (void)dwg_bs_read_rc(bs); /* flags 1 (splined-related): not modeled by dwg_polyline yet */
    flags2 = (unsigned char)dwg_bs_read_rc(bs);

    e = dwg_add_polyline(hDwg);
    if (e == NULL)
        return NULL;

    pl = dwg_polyline_from_entity(e);
    if (pl == NULL)
        return e;

    dwg_polyline_set_closed(pl, (flags2 & 0x01U) ? 1L : 0L);

    {
        unsigned long first_vertex_handle, last_vertex_handle, cur_handle;
        unsigned char code;
        unsigned long value;
        unsigned long count;

        skip_to_layer_handle(bs, ms_end_bit, common);
        dwg_bs_read_handle(bs, &code, &value); /* LAYER: already applied centrally by the caller */

        if (common->ltflags == 3UL)
            dwg_bs_read_handle(bs, &code, &value);
        if (common->plotstyleflags == 3UL)
            dwg_bs_read_handle(bs, &code, &value);

        dwg_bs_read_handle(bs, &code, &value);
        first_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);
        dwg_bs_read_handle(bs, &code, &value);
        last_vertex_handle = dwg_bs_resolve_handle(code, value, common->handle);

        cur_handle = first_vertex_handle;
        for (count = 0UL; count < DWG_R2000_MAX_VERTICES; count++)
        {
            unsigned long vloc, next_handle;
            int reached_last = (cur_handle == last_vertex_handle);

            if (!objmap_find(objmap, cur_handle, &vloc))
                break;

            if (!decode_vertex3d_and_advance(data, length, vloc, cur_handle, pl, &next_handle))
                break;

            if (reached_last)
                break;

            cur_handle = next_handle;
        }
    }

    return e;
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

HDWG dwg_read_dwg_r2000(const char *path, DWG_IO_RESULT *result)
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

    /* dwg_r2000_parse_header also accepts AC1012/AC1014 (R13/R14) now,
       since the two share R2000's exact header structure -- see
       dwg_r1314_reader.c, which relies on that to reuse this same
       header parser. But R13/R14's per-OBJECT bit layout is genuinely
       different (see reverse/DWG_R1314_format_reference.md), so THIS
       reader must reject them explicitly rather than proceeding to
       decode entities with the wrong field layout -- confirmed as a
       real bug (not just theoretical): without this check, a real R14
       file fed here hung/ran pathologically slowly instead of failing
       cleanly, because misaligned reads occasionally still produce
       small, "plausible-looking" type codes that trigger full entity
       decode + an O(n) LAYER lookup per (mis-)recognized entity. */
    if (!dwg_r2000_parse_header(data, length, &header) ||
        memcmp(header.version, "AC1015", 6) != 0)
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
        DWG_R2000_COMMON_ENTITY common;
        unsigned long ms_end_bit;
        HENTITY e = NULL;

        /* Defensive bounds check: a real, heavily-edited file's object
           map can contain stale entries whose location, despite passing
           the map's own section-level CRC, doesn't point at a real
           object -- confirmed against a real 4MB R14 sample while
           building the sibling R13/R14 reader (dwg_r1314_entity_reader.c);
           the same risk applies here, this just was never exercised by
           the small, clean R2000 sample files used to validate this
           reader originally. 6 bytes of margin covers the MS length (up
           to 4 bytes) + BS type (up to 2 bytes) header read below. */
        if (objmap.entries[i].location + 6UL >= length)
            continue;

        dwg_bs_init(&bs, data, length);
        dwg_bs_seek_bit(&bs, objmap.entries[i].location * 8UL);

        (void)dwg_bs_read_ms(&bs); /* entity length in bytes: framing only, not needed here */
        ms_end_bit = dwg_bs_tell_bit(&bs);
        obj_type = dwg_bs_read_bs(&bs);

        if (!read_common_entity_data(&bs, &common))
            continue; /* EED or graphics present: not handled by this first pass */

        switch (obj_type)
        {
        case DWG_R2000_TYPE_LINE:   e = decode_line(hDwg, &bs);   break;
        case DWG_R2000_TYPE_CIRCLE: e = decode_circle(hDwg, &bs); break;
        case DWG_R2000_TYPE_ARC:    e = decode_arc(hDwg, &bs);    break;
        case DWG_R2000_TYPE_POINT:  e = decode_point(hDwg, &bs);  break;
        case DWG_R2000_TYPE_SOLID:  e = decode_solid(hDwg, &bs);  break;
        case DWG_R2000_TYPE_INSERT:
            e = decode_insert(hDwg, &bs, ms_end_bit, &common, data, length, &objmap);
            break;
        case DWG_R2000_TYPE_POLYLINE2D:
            e = decode_polyline2d(hDwg, &bs, ms_end_bit, &common, data, length, &objmap);
            break;
        case DWG_R2000_TYPE_POLYLINE3D:
            e = decode_polyline3d(hDwg, &bs, ms_end_bit, &common, data, length, &objmap);
            break;
        case DWG_R2000_TYPE_MTEXT: e = decode_mtext(hDwg, &bs); break;
        case DWG_R2000_TYPE_TEXT:  e = decode_text(hDwg, &bs);  break;
        default: break; /* not modeled yet: skip via the object map, no need to know its length */
        }

        if (e == NULL)
            continue;

        apply_color(e, common.color);

        {
            unsigned long layer_handle, layer_loc;
            char layer_name[DWG_R2000_MAX_LAYER_NAME];
            unsigned short layer_color = 0U;

            if (read_entity_layer_handle(&bs, ms_end_bit, &common, &layer_handle) &&
                objmap_find(&objmap, layer_handle, &layer_loc) &&
                decode_table_record_name(data, length, layer_loc, DWG_R2000_TYPE_LAYER, layer_name, sizeof(layer_name), &layer_color))
            {
                dwg_entity_put_layer(e, layer_name);

                /* common.color==0/256 means BYBLOCK/BYLAYER -- apply_color
                   above deliberately left the entity's own color unset in
                   that case, so resolve the REAL color from its layer here
                   instead. Baking the resolved color into the entity (vs.
                   modeling a real HLAYER object with its own color that
                   the renderer would look up) is a deliberate simplification
                   -- this reader never creates real HLAYER objects for a
                   file's layers at all (dwg_entity_put_layer only stores
                   the name string on the entity), and doing so properly
                   would be a bigger, separate change; this gets real,
                   correct-looking colors on screen without that. */
                if (common.color == 0U || common.color == 256U)
                    apply_color(e, layer_color);
            }
        }

        if (obj_type == DWG_R2000_TYPE_MTEXT || obj_type == DWG_R2000_TYPE_TEXT)
        {
            unsigned long style_handle, style_loc;
            char style_name[DWG_R2000_MAX_STYLE_NAME];

            if (read_entity_style_handle(&bs, ms_end_bit, &common, &style_handle) &&
                objmap_find(&objmap, style_handle, &style_loc) &&
                decode_table_record_name(data, length, style_loc, DWG_R2000_TYPE_STYLE, style_name, sizeof(style_name), NULL))
            {
                if (obj_type == DWG_R2000_TYPE_MTEXT)
                    dwg_mtext_set_style_name(e, style_name);
                else
                    dwg_text_set_style_name(e, style_name);
            }
        }
    }

    dwg_r2000_objmap_free(&objmap);
    free(data);

    return hDwg;
}
