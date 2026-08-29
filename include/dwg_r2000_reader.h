#ifndef DWG_R2000_READER_H
#define DWG_R2000_READER_H

#include "dwg_types.h"
#include "dwg_file_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2000 (AC1015) file-header and object-map parsing. Ported from the
 * validated Python reference (reverse/dwg_r2000_bitstream_check.py's
 * parse_header/parse_object_map, cross-checked against real sample
 * files) -- see reverse/DWG_R2000_format_reference.md for the full
 * field-by-field derivation. This is the layer above dwg_bitstream.h:
 * it locates *where* objects live in the file; dwg_bitstream.h then
 * decodes the bits at each location.
 *
 * File header fields below are all byte-aligned at fixed offsets, so
 * they're read directly rather than through DWG_BITSTREAM.
 */

#define DWG_R2000_MAX_RECORDS 8 /* spec: "up to 6 sets... seen", some headroom */

typedef struct
{
    unsigned char record_number;
    unsigned long seeker; /* absolute byte offset of this section */
    unsigned long size;   /* section size in bytes */
} DWG_R2000_SECTION_RECORD;

typedef struct
{
    char version[7]; /* e.g. "AC1015", null-terminated */
    unsigned long image_seeker;
    unsigned short codepage;
    unsigned long record_count;
    DWG_R2000_SECTION_RECORD records[DWG_R2000_MAX_RECORDS];
} DWG_R2000_HEADER;

/*
 * Parses the fixed-layout file header (version string, image seeker,
 * codepage, section-locator records). Returns 1 on success, 0 if the
 * buffer is too short to even hold the header or the version isn't
 * "AC1015" (this reader targets R2000 specifically; a different
 * version string is a caller error, not something to silently
 * misparse). Does not validate the header's own CRC/sentinel (see
 * reverse/DWG_R2000_format_reference.md if that's ever needed --
 * not required just to locate sections).
 */
long dwg_r2000_parse_header(const unsigned char *data, unsigned long length,
                            DWG_R2000_HEADER *out);

/* Finds the section record with the given record_number (e.g. 2 for
   the object map) in an already-parsed header. Returns NULL if not
   present. */
const DWG_R2000_SECTION_RECORD *dwg_r2000_find_record(const DWG_R2000_HEADER *header,
                                                       unsigned char record_number);

typedef struct
{
    unsigned long handle;
    unsigned long location; /* absolute byte offset of this object in the file */
} DWG_R2000_OBJMAP_ENTRY;

typedef struct
{
    DWG_R2000_OBJMAP_ENTRY *entries;
    unsigned long count;
} DWG_R2000_OBJMAP;

/*
 * Parses the object map (spec p.251): repeated sub-sections, each a
 * BIG-ENDIAN 2-byte size (covering only itself + the pair data, NOT
 * the trailing CRC -- confirmed empirically against a real file,
 * see reverse/DWG_R2000_format_reference.md's "object map CRC
 * placement" note), then MC-encoded (handle-offset, location-offset)
 * pairs accumulated onto running totals, then a separate BIG-ENDIAN
 * 2-byte CRC immediately after. Terminates at a sub-section whose
 * declared size is exactly 2 (CRC only, no pairs). The size/CRC fields
 * being big-endian is a real exception to this format's usual
 * little-endian convention, easy to get backwards; MC payload bytes
 * themselves are read the normal way (see dwg_bitstream.h).
 *
 * The "-2" margin used internally while reading pairs is only a soft
 * stop-requesting-a-new-pair heuristic, not an exact boundary: since MC
 * values are variable-length, a pair read already in progress can (and
 * in real data does) land exactly at or past that margin in one step --
 * this is expected, not a bug, and the real per-subsection end
 * (where the CRC actually starts) is wherever the pair reads
 * naturally stop, not a fixed offset.
 *
 * seeker/size come from the header's record_number==2 entry (find it
 * with dwg_r2000_find_record first). Allocates out->entries (caller
 * must free with dwg_r2000_objmap_free). Returns 1 on success (even if
 * count ends up 0), 0 on allocation failure.
 */
long dwg_r2000_parse_object_map(const unsigned char *data, unsigned long length,
                                unsigned long seeker, unsigned long size,
                                DWG_R2000_OBJMAP *out);

/*
 * Same object-map parsing as dwg_r2000_parse_object_map (identical
 * section/CRC framing, shared by R13/R14/R2000 -- see dwg_r2000_
 * parse_header's own comment), but for the handle-drift resync and
 * per-object structural validation steps, uses R13/R14's real field
 * order (Type -> Handle directly) instead of R2000's (Type -> obj_
 * size -> Handle). Use this from the R13/R14 reader instead of
 * dwg_r2000_parse_object_map -- using the wrong one silently
 * misreads every object's own handle field for validation purposes
 * (a real, confirmed bug this project already hit once with the
 * carving repair before this function existed).
 */
long dwg_r1314_parse_object_map(const unsigned char *data, unsigned long length,
                                unsigned long seeker, unsigned long size,
                                DWG_R2000_OBJMAP *out);

void dwg_r2000_objmap_free(DWG_R2000_OBJMAP *map);

/*
 * Reads an R2000 (AC1015) file end to end: header, object map, then
 * every object in the map whose type this engine currently models
 * (LINE/CIRCLE/ARC/POINT/TEXT/MTEXT/SOLID/INSERT/POLYLINE(2D+3D) --
 * see dwg_r2000_entity_reader.c), populating a new HDWG the same way
 * dwg_read_dwg_r12 does. Unmodeled object types (tables other than
 * LAYER/STYLE/BLOCK_HEADER name lookups, ATTRIB/ATTDEF, HATCH, other
 * non-fixed-type entities) are silently skipped, same "trust the
 * framing, skip what you don't understand" convention the R12 reader
 * already uses.
 *
 * LAYER and STYLE ARE resolved to their real names (not left on the
 * default "0"/no-style) by following each entity's handle reference
 * into the file's own LAYER (0x33) / STYLE (0x35) table records --
 * see read_entity_layer_handle/read_entity_style_handle and
 * decode_table_record_name in dwg_r2000_entity_reader.c. ARC's start/
 * end angle ARE correctly converted from the file's native radians to
 * this engine's degrees convention (see dwg_file_io.h's own angle
 * comment) -- confirmed against the real samples in
 * reverse/DWG_R2000_format_reference.md. POINT's own X-axis angle
 * field is read (to stay correctly positioned in the bitstream) but
 * discarded -- DWG_POINT3D has no field to store it in, same "not
 * modeled" simplification the R12 reader/writer already accept for
 * this rarely-used display-only property.
 *
 * The companion writer (dwg_write_dwg_r2000, dwg_r2000_writer.h) can
 * write back every entity type this reader understands, against a
 * real template file -- see reverse/DWG_R2000_writer_plan.md.
 */
HDWG dwg_read_dwg_r2000(const char *path, DWG_IO_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif
