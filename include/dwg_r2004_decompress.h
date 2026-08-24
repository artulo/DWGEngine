#ifndef DWG_R2004_DECOMPRESS_H
#define DWG_R2004_DECOMPRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2004+ (AC1018-AC1032) LZ77-variant decompressor, per the ODA "Open
 * Design Specification for .dwg files" section 4.7. Ported from the
 * validated Python reference (reverse/dwg_r2004_decompress.py), which
 * was cross-checked against 7+ real pages of Arturo's own ROTATORIO.dwg
 * (AC1032/R2018) -- see reverse/DWG_R2004plus_format_reference.md for
 * the full derivation, including 2 real spec-transcription bugs found
 * and fixed along the way (both already folded into this port):
 *   - "Two Byte Offset" needs a +1 the spec's own text omits.
 *   - "Literal Length" byte range is 0x01-0x0F, not 0x01-0x0E as
 *     literally transcribed (caught by cross-checking the spec's own
 *     stated result range).
 *
 * Unlike the Python prototype (which trusts Python's own IndexError to
 * catch overruns), this port bounds-checks every read against
 * comp_size and every write against out_capacity explicitly, since
 * comp is untrusted file data.
 *
 * Returns the number of bytes actually written to out (may be less
 * than decompressed_size if the stream's own terminator opcode (0x11)
 * is hit early -- same "trust the framing, stop when told to" contract
 * every other reader in this engine uses), or -1 if the compressed
 * stream is malformed (an out-of-range read/write, an unused opcode,
 * or a back-reference before the start of the output).
 */
long dwg_r2004_decompress(const unsigned char *comp, unsigned long comp_size,
                          unsigned char *out, unsigned long out_capacity,
                          unsigned long decompressed_size);

#ifdef __cplusplus
}
#endif

#endif
