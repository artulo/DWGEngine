#ifndef DWG_R1314_READER_H
#define DWG_R1314_READER_H

#include "dwg_types.h"
#include "dwg_file_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AutoCAD R13 (AC1012) / R14 (AC1014) binary reader.
 *
 * File header and object map are BYTE-IDENTICAL to R2000's (confirmed
 * empirically against a real R14 file down to the exact 16-byte
 * sentinel and the object-map's CRC-16 algorithm -- the ODA spec's own
 * chapter 3/23 describe these as one shared "R13-R15" file structure),
 * so this reader calls straight into dwg_r2000_reader.h's
 * dwg_r2000_parse_header/dwg_r2000_parse_object_map rather than
 * duplicating them -- only the per-OBJECT bit-level encoding (Common
 * Entity Data field order, LINE/TEXT's field layout, BitThickness/
 * BitExtrusion being plain unconditional fields instead of R2000's bit-
 * gated forms) genuinely differs, see reverse/DWG_R1314_format_reference.md.
 *
 * Real-file finding worth knowing before trusting object counts: a
 * heavily-edited real R14 file's object map can contain a large
 * fraction of stale/orphaned entries whose declared location, while
 * passing the section-level CRC check, doesn't correspond to a real
 * object (some point past EOF entirely). This reader defensively
 * bounds-checks every object-map location against the file size and
 * validates the decoded type is one it actually models before trusting
 * it, silently skipping anything that doesn't check out -- same "trust
 * the framing, skip what you don't understand" convention already used
 * for unmodeled entity TYPES, extended here to also cover malformed/
 * stale MAP ENTRIES. Confirmed this still yields a large, plausible set
 * of real entities from a real 4MB architectural file (hundreds of real
 * LINEs etc.), not just an empty document.
 */
HDWG dwg_read_dwg_r1314(const char *path, DWG_IO_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif
