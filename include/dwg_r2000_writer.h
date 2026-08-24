#ifndef DWG_R2000_WRITER_H
#define DWG_R2000_WRITER_H

#include "dwg_types.h"
#include "dwg_file_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2000 (AC1015) writer: "template + injection" architecture. Ported
 * from the validated Python prototype
 * (reverse/dwg_r2000_writer_prototype.py + reverse/dwg_r2000_bitwriter.py),
 * itself validated by round-tripping its output through this engine's
 * own dwg_read_dwg_r2000 -- see reverse/DWG_R2000_writer_plan.md for
 * the full architecture writeup and every real bug found deriving it
 * (MC signed-encoding, an object-map reader off-by-one shared with
 * dwg_r2000_reader.c, obj_size measured from the wrong bit range).
 *
 * Loads template_path as an immutable byte template, replaces
 * whatever entities *Model_Space currently owns with hDwg's own
 * entity list (LINE/CIRCLE/ARC/POINT/TEXT/MTEXT/SOLID/INSERT/POLYLINE
 * -- every type this engine's R2000 reader also understands),
 * preserves every other object in the template verbatim (just shifted
 * in file position), regenerates the object map, and patches the file
 * header's own section-locator pointers + CRC. LAYER/STYLE names are
 * first resolved against the template's own table records; an entity
 * with no layer/style set at all (the empty string, dwg_entity_create's
 * own default) resolves to "0"/"Standard" specifically -- but any
 * other name not found in the template is not silently substituted,
 * it causes a brand-new LAYER/STYLE object to be created under that
 * exact name (LAYER_CONTROL/STYLE_CONTROL rewritten to reference it),
 * cloning an existing template record's color/linetype/font/height/etc
 * verbatim (this writer has no way to specify different values for a
 * newly-created record -- see dwg_r2000_writer.c's own comments on
 * resolve_or_create_layer/resolve_or_create_style for why). If the
 * template has no usable LAYER_CONTROL/STYLE_CONTROL to extend, that
 * name can't be created and the whole write fails. INSERT's block name
 * has no create fallback at all: if the referenced block doesn't exist
 * in the template, the write fails rather than silently writing a
 * dangling reference.
 *
 * DWG_IO_OK on success; DWG_IO_ERROR_OPEN if the template or output
 * path couldn't be opened; DWG_IO_ERROR_FORMAT if the template isn't a
 * real R2000 file, has no *Model_Space, or hDwg has an INSERT
 * referencing a block the template doesn't define; DWG_IO_ERROR_MEMORY
 * on allocation failure.
 */
DWG_IO_RESULT dwg_write_dwg_r2000(HDWG hDwg, const char *template_path, const char *out_path);

#ifdef __cplusplus
}
#endif

#endif
