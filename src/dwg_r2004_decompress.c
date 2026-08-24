#include "dwg_r2004_decompress.h"

/* See the header comment and reverse/dwg_r2004_decompress.py (the
   validated Python reference this was ported from) for the full
   derivation and the 2 real spec-transcription bugs already folded
   in here. */

typedef struct
{
    const unsigned char *data;
    unsigned long size;
    unsigned long pos;
    int error;
} R2004_SRC;

static unsigned char src_byte(R2004_SRC *s)
{
    if (s->pos >= s->size)
    {
        s->error = 1;
        return 0U;
    }
    return s->data[s->pos++];
}

/* Returns (length, consumed-ok); on a "not a literal length byte"
   result (high nibble set), rewinds pos by 1 so the caller re-reads
   that byte as opcode1, same as the Python reference's `pos - 1`. */
static unsigned long read_literal_length(R2004_SRC *s)
{
    unsigned char b = src_byte(s);
    if (s->error) return 0UL;

    if (b == 0x00U)
    {
        unsigned long total = 0x0FUL;
        for (;;)
        {
            unsigned char b2 = src_byte(s);
            if (s->error) return 0UL;
            if (b2 == 0x00U)
                total += 0xFFUL;
            else
            {
                total += b2;
                break;
            }
        }
        return total + 3UL;
    }
    else if (b >= 0x01U && b <= 0x0FU)
    {
        return (unsigned long)b + 3UL;
    }
    else
    {
        s->pos--; /* not consumed: this byte is the next opcode1 */
        return 0UL;
    }
}

static unsigned long read_long_compression_offset(R2004_SRC *s)
{
    unsigned char b = src_byte(s);
    if (s->error) return 0UL;

    if (b == 0x00U)
    {
        unsigned long total = 0xFFUL;
        for (;;)
        {
            unsigned char b2 = src_byte(s);
            if (s->error) return 0UL;
            if (b2 == 0x00U)
                total += 0xFFUL;
            else
            {
                total += b2;
                break;
            }
        }
        return total;
    }
    else
        return (unsigned long)b;
}

/* The +1 here is the real, spec-omitted fix -- see the header comment. */
static unsigned long read_two_byte_offset(R2004_SRC *s, unsigned long *out_litcount)
{
    unsigned char first, second;
    unsigned long offset;

    first = src_byte(s);
    if (s->error) { *out_litcount = 0UL; return 0UL; }
    second = src_byte(s);
    if (s->error) { *out_litcount = 0UL; return 0UL; }

    offset = ((unsigned long)first >> 2) | ((unsigned long)second << 6);
    offset += 1UL;
    *out_litcount = (unsigned long)(first & 0x03U);
    return offset;
}

long dwg_r2004_decompress(const unsigned char *comp, unsigned long comp_size,
                          unsigned char *out, unsigned long out_capacity,
                          unsigned long decompressed_size)
{
    R2004_SRC s;
    unsigned long out_len = 0UL;
    unsigned long lit_len;

    s.data = comp;
    s.size = comp_size;
    s.pos = 0UL;
    s.error = 0;

    if (decompressed_size > out_capacity)
        decompressed_size = out_capacity;

    lit_len = read_literal_length(&s);
    if (s.error) return -1L;

    if (lit_len > comp_size - s.pos || out_len + lit_len > out_capacity)
        return -1L;
    {
        unsigned long k;
        for (k = 0UL; k < lit_len; k++)
            out[out_len++] = comp[s.pos++];
    }

    for (;;)
    {
        unsigned char opcode1;
        unsigned long compressedBytes, compOffset, litCount;
        unsigned long copy_start, k;

        if (out_len >= decompressed_size)
            break;

        if (s.pos >= s.size)
            break;

        opcode1 = src_byte(&s);
        if (s.error) return -1L;

        if (opcode1 == 0x11U)
            break;
        else if (opcode1 <= 0x0FU)
            return -1L; /* unused opcode */
        else if (opcode1 == 0x10U)
        {
            unsigned long cb = read_long_compression_offset(&s);
            if (s.error) return -1L;
            compressedBytes = cb + 9UL;
            compOffset = read_two_byte_offset(&s, &litCount) + 0x3FFFUL;
            if (s.error) return -1L;
            if (litCount == 0UL)
            {
                litCount = read_literal_length(&s);
                if (s.error) return -1L;
            }
        }
        else if (opcode1 >= 0x12U && opcode1 <= 0x1FU)
        {
            compressedBytes = (unsigned long)(opcode1 & 0x0FU) + 2UL;
            compOffset = read_two_byte_offset(&s, &litCount) + 0x3FFFUL;
            if (s.error) return -1L;
            if (litCount == 0UL)
            {
                litCount = read_literal_length(&s);
                if (s.error) return -1L;
            }
        }
        else if (opcode1 == 0x20U)
        {
            unsigned long cb = read_long_compression_offset(&s);
            if (s.error) return -1L;
            compressedBytes = cb + 0x21UL;
            compOffset = read_two_byte_offset(&s, &litCount);
            if (s.error) return -1L;
            if (litCount == 0UL)
            {
                litCount = read_literal_length(&s);
                if (s.error) return -1L;
            }
        }
        else if (opcode1 >= 0x21U && opcode1 <= 0x3FU)
        {
            compressedBytes = (unsigned long)opcode1 - 0x1EUL;
            compOffset = read_two_byte_offset(&s, &litCount);
            if (s.error) return -1L;
            if (litCount == 0UL)
            {
                litCount = read_literal_length(&s);
                if (s.error) return -1L;
            }
        }
        else /* 0x40 <= opcode1 <= 0xFF */
        {
            unsigned char opcode2;
            unsigned long lc_code;

            compressedBytes = (unsigned long)((opcode1 & 0xF0U) >> 4) - 1UL;
            opcode2 = src_byte(&s);
            if (s.error) return -1L;
            compOffset = (((unsigned long)opcode2 << 2) | (((unsigned long)opcode1 & 0x0CU) >> 2)) + 1UL;
            lc_code = (unsigned long)(opcode1 & 0x03U);
            if (lc_code == 0UL)
            {
                litCount = read_literal_length(&s);
                if (s.error) return -1L;
            }
            else
                litCount = lc_code;
        }

        if (compOffset > out_len)
            return -1L; /* back-reference before the start of output */
        copy_start = out_len - compOffset;

        if (compressedBytes > out_capacity - out_len)
            return -1L;
        for (k = 0UL; k < compressedBytes; k++)
        {
            out[out_len] = out[copy_start + k];
            out_len++;
        }

        if (litCount > comp_size - s.pos || litCount > out_capacity - out_len)
            return -1L;
        for (k = 0UL; k < litCount; k++)
            out[out_len++] = comp[s.pos++];
    }

    return (long)out_len;
}
