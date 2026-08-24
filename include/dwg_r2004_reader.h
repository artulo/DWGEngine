#ifndef DWG_R2004_READER_H
#define DWG_R2004_READER_H

#include "dwg_types.h"
#include "dwg_file_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2004+ (AC1018-AC1032) reader. Ported from the validated Python
 * investigation prototype -- see reverse/DWG_R2004plus_format_reference.md
 * for the full derivation (encrypted file header, paged/compressed
 * section system, the AcDb:Handles object map, and the R2010+ Common
 * Entity Data field layout -- 3 real spec-transcription bugs and 2 real
 * "missing field" bugs found and fixed along the way).
 *
 * Scoped to AC1024 (R2010), AC1027 (R2013), and AC1032 (R2018)
 * specifically -- the only versions this engine has real, validated
 * samples for (Arturo's ROTATORIO.dwg is AC1032). AC1018 (R2004) and
 * AC1021 (R2007) are deliberately rejected: R2007 uses a completely
 * different, much harder architecture (Reed-Solomon FEC, a different
 * compressor) per the spec's own text, and AC1018's entity bit-coding
 * (whether it has the R2010+ handlestream_size/no-physical-obj-size
 * fields this reader assumes) has never been checked against a real
 * file -- same "don't build untested speculative branches" discipline
 * the rest of this engine follows.
 *
 * Currently models LINE/CIRCLE/ARC/POINT/SOLID (the same first 5 basic
 * types R13/14's reader started with) -- confirmed against 496/519
 * (95.6%) real objects in ROTATORIO.dwg producing a Handle that exactly
 * matches the independently-cracked AcDb:Handles map, with genuinely
 * plausible decoded geometry (not just a matching handle). TEXT/MTEXT/
 * INSERT/POLYLINE are NOT modeled yet -- TEXT/MTEXT/INSERT all need
 * resolving a real string (a text value or a referenced block/layer
 * name) via R2007+'s separate "string stream" substream, whose exact
 * byte offset this reader hasn't cracked yet (a real, deeper blocker
 * than anything else in this reader -- see the format reference doc's
 * matching section); POLYLINE2D is confirmed simply absent from
 * ROTATORIO.dwg's own real handle set, so adding it has no sample to
 * validate against yet. Layer/color-name resolution via the handle
 * stream is likewise blocked on the same string-stream issue (colors
 * are the raw ACI/ENC index only, same simplification R13/14 started
 * with before layer resolution was added) -- entities decode with
 * default layer "0" and whatever raw ACI color their own Common Entity
 * Data carries.
 */
HDWG dwg_read_dwg_r2004(const char *path, DWG_IO_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif
