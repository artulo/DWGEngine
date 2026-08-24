#ifndef DWG_BITSTREAM_H
#define DWG_BITSTREAM_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bit-level cursor over a byte buffer, implementing the primitive data
 * types from the Open Design Alliance's public "Open Design
 * Specification for .dwg files" (see reverse/DWG_R2000_format_reference.md
 * for the full derivation, worked examples, and real-file validation
 * this was ported from -- reverse/dwg_r2000_bitstream_check.py is the
 * Python reference implementation these functions were cross-checked
 * against bit-for-bit before this port).
 *
 * Every read function advances the cursor and returns the decoded
 * value; none of them validate that enough data remains (the caller is
 * expected to know the object's declared size and stay within it, same
 * trust-the-framing convention dwg_dwg_reader.c already uses for R12).
 *
 * Two real gotchas, easy to get backwards (both bit us during the
 * Python validation before this port, see the reference doc):
 *   - Multi-byte RAW values (RC/RS/RL/RD, and the raw forms of BS/BL/
 *     BD) are little-endian: read 8-bit groups MSB-first each, then
 *     combine the groups little-endian (first group = least
 *     significant byte). This is NOT the same as reading the whole
 *     span as one big MSB-first binary number.
 *   - MC (modular char, byte-granular) and MS (modular short,
 *     16-bit-granular) are different algorithms, not the same thing at
 *     a different unit size -- the entity Length field is MS, not MC;
 *     using MC there silently misaligns everything after it.
 */

typedef struct
{
    const unsigned char *data;
    unsigned long length;   /* total bytes available in data */
    unsigned long bitpos;   /* current position, in bits, from the start of data */
} DWG_BITSTREAM;

void dwg_bs_init(DWG_BITSTREAM *bs, const unsigned char *data, unsigned long length);

/* Absolute positioning, both in bits from the start of data. */
void dwg_bs_seek_bit(DWG_BITSTREAM *bs, unsigned long bitpos);
unsigned long dwg_bs_tell_bit(DWG_BITSTREAM *bs);

/* B: a single bit (0 or 1). */
unsigned long dwg_bs_read_bit(DWG_BITSTREAM *bs);

/* BB: 2-bit code. */
unsigned long dwg_bs_read_bb(DWG_BITSTREAM *bs);

/* Reads n bits (n <= 32) as a plain big-binary-number bit
   concatenation. Only meaningful for genuinely single-value bitfields
   (flag bits, BB/3B-style prefixes) -- NOT for multi-byte raw values,
   see dwg_bs_read_raw_le below for those. */
unsigned long dwg_bs_read_bits(DWG_BITSTREAM *bs, unsigned long n);

/* Reads nbytes 8-bit groups (nbytes <= 8) from the current, possibly
   non-byte-aligned position, and writes them into out[] in
   little-endian order (out[0] = least significant byte) -- the actual
   rule behind every RAW multi-byte read in this format. */
void dwg_bs_read_raw_le(DWG_BITSTREAM *bs, unsigned char *out, unsigned long nbytes);

/* RC/RS/RL/RD: raw (uncompressed), little-endian, any bit position. */
unsigned char dwg_bs_read_rc(DWG_BITSTREAM *bs);
unsigned short dwg_bs_read_rs(DWG_BITSTREAM *bs);
unsigned long dwg_bs_read_rl(DWG_BITSTREAM *bs);
double dwg_bs_read_rd(DWG_BITSTREAM *bs);

/* BS/BL/BD: compressed forms (see the .c file / reference doc for the
   2-bit opcode meanings). */
unsigned short dwg_bs_read_bs(DWG_BITSTREAM *bs);
unsigned long dwg_bs_read_bl(DWG_BITSTREAM *bs);
double dwg_bs_read_bd(DWG_BITSTREAM *bs);

/* DD: BitDouble with default -- default_value is patched into per the
   2-bit opcode (no data / low 4 bytes / low 4 + bytes 5-6 / full RD). */
double dwg_bs_read_dd(DWG_BITSTREAM *bs, double default_value);

/* 3BD: three consecutive BD values as a point. */
void dwg_bs_read_3bd(DWG_BITSTREAM *bs, DWG_POINT3D *out);

/* BE (BitExtrusion): 1 bit; if 1, extrusion is (0,0,1) with no further
   data; if 0, a 3BD follows with the real vector. */
void dwg_bs_read_be(DWG_BITSTREAM *bs, DWG_POINT3D *out);

/* BT (BitThickness): 1 bit; if 1, thickness is 0.0; if 0, a BD follows. */
double dwg_bs_read_bt(DWG_BITSTREAM *bs);

/* MC (modular char): compressed integer, byte-granular continuation
   flag (bit 0x80). If negatable is nonzero, the terminating byte's
   0x40 bit (if set) negates the result (used for signed offsets, e.g.
   object-map location deltas; handle-offset deltas in the object map
   are never negative and should pass negatable=0). */
long dwg_bs_read_mc(DWG_BITSTREAM *bs, int negatable);

/* MS (modular short): compressed integer, 16-bit-short-granular
   continuation flag (bit 0x8000), 15 payload bits per short. Used for
   the entity Length field and other size fields -- NOT the same
   algorithm as MC, see the header comment above. */
unsigned long dwg_bs_read_ms(DWG_BITSTREAM *bs);

/* H (handle reference): CODE(4 bits).COUNTER(4 bits).bytes, COUNTER
   bytes read in natural (big-endian-ish) order -- NOT little-endian
   like the raw numeric types, a real and easy-to-miss distinction. See
   reverse/DWG_R2000_format_reference.md's handle-code table for what
   *code* means (absolute vs. relative-to-this-object's-own-handle).
   *value's* meaning depends on *code -- pass both to
   dwg_bs_resolve_handle to get the actual referenced handle. */
void dwg_bs_read_handle(DWG_BITSTREAM *bs, unsigned char *code, unsigned long *value);

/*
 * Resolves a handle reference's real target given the (code, value)
 * dwg_bs_read_handle produced and the handle of the object the
 * reference was read from ("reference_handle" -- e.g. a VERTEX's own
 * handle, when resolving that same VERTEX's "next" link). Codes
 * 0x2-0x5 (soft/hard ownership/pointer) are absolute: returns value
 * unchanged. Codes 0x6/0x8/0xA/0xC are relative
 * (reference_handle+1 / -1 / +value / -value respectively) -- this is
 * how, e.g., a POLYLINE's VERTEX chain links each vertex to the next
 * (confirmed against a real file: a VERTEX with handle 496 pointed to
 * its own successor via code 0x6 with value 0, meaning "496+1=497",
 * not literally 0). Any other code returns value unchanged (no known
 * relative meaning).
 */
unsigned long dwg_bs_resolve_handle(unsigned char code, unsigned long value,
                                    unsigned long reference_handle);

/* T (text): BS length, then that many raw bytes (no null terminator in
   the stream). Copies up to buf_size-1 bytes into buf and always
   null-terminates; returns the field's real length (which may exceed
   what was copied, if buf was too small -- caller can detect
   truncation by comparing the return value against buf_size). */
unsigned short dwg_bs_read_t(DWG_BITSTREAM *bs, char *buf, unsigned short buf_size);

#ifdef __cplusplus
}
#endif

#endif
