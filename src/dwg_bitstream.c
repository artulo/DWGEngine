#include <string.h>

#include "dwg_bitstream.h"

void dwg_bs_init(DWG_BITSTREAM *bs, const unsigned char *data, unsigned long length)
{
    bs->data = data;
    bs->length = length;
    bs->bitpos = 0UL;
}

void dwg_bs_seek_bit(DWG_BITSTREAM *bs, unsigned long bitpos)
{
    bs->bitpos = bitpos;
}

unsigned long dwg_bs_tell_bit(DWG_BITSTREAM *bs)
{
    return bs->bitpos;
}

unsigned long dwg_bs_read_bit(DWG_BITSTREAM *bs)
{
    unsigned long byte_i = bs->bitpos / 8UL;
    unsigned long bit_i;
    unsigned long v;

    /* Bounds check added after a real crash: every other read function
       funnels through this one, and callers are normally trusted to
       stay within an object's own declared framing (see the header
       comment) -- but a STALE/malformed object-map entry (confirmed to
       occur in real, heavily-edited files, see dwg_r1314_reader.h) can
       point at garbage bytes whose MC/MS continuation bit chain never
       naturally terminates within the real buffer, running this past
       bs->length and corrupting memory. Returning 0 past the end is
       deliberately chosen, not arbitrary: MC/MS's continuation flag IS
       the top bit of each unit, so a synthetic 0 here makes any
       runaway read terminate on its own next step instead of reading
       further out of bounds. */
    if (byte_i >= bs->length)
    {
        bs->bitpos += 1UL;
        return 0UL;
    }

    bit_i = 7UL - (bs->bitpos % 8UL);
    v = ((unsigned long)bs->data[byte_i] >> bit_i) & 1UL;

    bs->bitpos += 1UL;
    return v;
}

unsigned long dwg_bs_read_bits(DWG_BITSTREAM *bs, unsigned long n)
{
    unsigned long v = 0UL;
    unsigned long i;

    for (i = 0UL; i < n; i++)
        v = (v << 1) | dwg_bs_read_bit(bs);

    return v;
}

unsigned long dwg_bs_read_bb(DWG_BITSTREAM *bs)
{
    return dwg_bs_read_bits(bs, 2UL);
}

void dwg_bs_read_raw_le(DWG_BITSTREAM *bs, unsigned char *out, unsigned long nbytes)
{
    unsigned long i;

    for (i = 0UL; i < nbytes; i++)
        out[i] = (unsigned char)dwg_bs_read_bits(bs, 8UL);
}

unsigned char dwg_bs_read_rc(DWG_BITSTREAM *bs)
{
    return (unsigned char)dwg_bs_read_bits(bs, 8UL);
}

unsigned short dwg_bs_read_rs(DWG_BITSTREAM *bs)
{
    unsigned char raw[2];

    dwg_bs_read_raw_le(bs, raw, 2UL);
    return (unsigned short)(raw[0] | ((unsigned short)raw[1] << 8));
}

unsigned long dwg_bs_read_rl(DWG_BITSTREAM *bs)
{
    unsigned char raw[4];

    dwg_bs_read_raw_le(bs, raw, 4UL);
    return (unsigned long)raw[0] | ((unsigned long)raw[1] << 8) |
           ((unsigned long)raw[2] << 16) | ((unsigned long)raw[3] << 24);
}

double dwg_bs_read_rd(DWG_BITSTREAM *bs)
{
    unsigned char raw[8];
    double v;

    dwg_bs_read_raw_le(bs, raw, 8UL);
    memcpy(&v, raw, 8UL); /* x86 is little-endian, matches the format's own byte order */
    return v;
}

unsigned short dwg_bs_read_bs(DWG_BITSTREAM *bs)
{
    unsigned long code = dwg_bs_read_bb(bs);

    switch (code)
    {
    case 0UL: return dwg_bs_read_rs(bs);
    case 1UL: return (unsigned short)dwg_bs_read_bits(bs, 8UL);
    case 2UL: return 0U;
    default:  return 256U;
    }
}

unsigned long dwg_bs_read_bl(DWG_BITSTREAM *bs)
{
    unsigned long code = dwg_bs_read_bb(bs);

    switch (code)
    {
    case 0UL: return dwg_bs_read_rl(bs);
    case 1UL: return dwg_bs_read_bits(bs, 8UL);
    default:  return 0UL; /* code 2 = 0; code 3 is documented as unused */
    }
}

double dwg_bs_read_bd(DWG_BITSTREAM *bs)
{
    unsigned long code = dwg_bs_read_bb(bs);

    switch (code)
    {
    case 0UL: return dwg_bs_read_rd(bs);
    case 1UL: return 1.0;
    default:  return 0.0; /* code 2 = 0.0; code 3 is documented as unused */
    }
}

double dwg_bs_read_dd(DWG_BITSTREAM *bs, double default_value)
{
    unsigned long code = dwg_bs_read_bb(bs);
    unsigned char patched[8];

    switch (code)
    {
    case 0UL:
        return default_value;

    case 1UL:
        memcpy(patched, &default_value, 8UL);
        dwg_bs_read_raw_le(bs, patched, 4UL); /* replaces bytes 0-3 */
        {
            double v;
            memcpy(&v, patched, 8UL);
            return v;
        }

    case 2UL:
        /* spec: 2 data bytes replace bytes 5-6 of the default, then 4
           data bytes replace bytes 1-4 -- data is read in that order
           (2 bytes, then 4 bytes) */
        memcpy(patched, &default_value, 8UL);
        {
            unsigned char b56[2];
            unsigned char b14[4];

            dwg_bs_read_raw_le(bs, b56, 2UL);
            dwg_bs_read_raw_le(bs, b14, 4UL);
            memcpy(patched, b14, 4UL);
            memcpy(patched + 4, b56, 2UL);
        }
        {
            double v;
            memcpy(&v, patched, 8UL);
            return v;
        }

    default:
        return dwg_bs_read_rd(bs);
    }
}

void dwg_bs_read_3bd(DWG_BITSTREAM *bs, DWG_POINT3D *out)
{
    out->x = dwg_bs_read_bd(bs);
    out->y = dwg_bs_read_bd(bs);
    out->z = dwg_bs_read_bd(bs);
}

void dwg_bs_read_be(DWG_BITSTREAM *bs, DWG_POINT3D *out)
{
    if (dwg_bs_read_bit(bs) == 1UL)
    {
        out->x = 0.0;
        out->y = 0.0;
        out->z = 1.0;
        return;
    }

    dwg_bs_read_3bd(bs, out);
}

double dwg_bs_read_bt(DWG_BITSTREAM *bs)
{
    if (dwg_bs_read_bit(bs) == 1UL)
        return 0.0;

    return dwg_bs_read_bd(bs);
}

long dwg_bs_read_mc(DWG_BITSTREAM *bs, int negatable)
{
    unsigned char bytes_read[16]; /* generous; real MC values never need this many */
    unsigned long count = 0UL;
    unsigned char b;
    long value = 0L;
    int negative = 0;
    unsigned long i;

    do
    {
        b = (unsigned char)dwg_bs_read_bits(bs, 8UL);
        bytes_read[count] = b;
        count++;
    } while ((b & 0x80U) != 0U && count < sizeof(bytes_read));

    if (negatable && (bytes_read[count - 1UL] & 0x40U) != 0U)
    {
        negative = 1;
        bytes_read[count - 1UL] = (unsigned char)(bytes_read[count - 1UL] & ~0x40U);
    }

    for (i = 0UL; i < count; i++)
    {
        unsigned long payload = (unsigned long)(bytes_read[i] & 0x7FU);
        value += (long)(payload << (7UL * i));
    }

    return negative ? -value : value;
}

unsigned long dwg_bs_read_ms(DWG_BITSTREAM *bs)
{
    unsigned short shorts_read[8]; /* generous; two shorts (30 bits) is the documented practical max */
    unsigned long count = 0UL;
    unsigned short s;
    unsigned long value = 0UL;
    unsigned long i;

    do
    {
        s = dwg_bs_read_rs(bs);
        shorts_read[count] = s;
        count++;
    } while ((s & 0x8000U) != 0U && count < (sizeof(shorts_read) / sizeof(shorts_read[0])));

    for (i = 0UL; i < count; i++)
        value |= (unsigned long)(shorts_read[i] & 0x7FFFU) << (15UL * i);

    return value;
}

void dwg_bs_read_handle(DWG_BITSTREAM *bs, unsigned char *code, unsigned long *value)
{
    unsigned char b0 = dwg_bs_read_rc(bs);
    unsigned char counter = (unsigned char)(b0 & 0x0FU);
    unsigned long v = 0UL;
    unsigned char i;

    *code = (unsigned char)((b0 >> 4) & 0x0FU);

    for (i = 0U; i < counter; i++)
        v = (v << 8) | dwg_bs_read_rc(bs);

    *value = v;
}

unsigned long dwg_bs_resolve_handle(unsigned char code, unsigned long value,
                                    unsigned long reference_handle)
{
    switch (code)
    {
    case 0x06U: return reference_handle + 1UL;
    case 0x08U: return reference_handle - 1UL;
    case 0x0AU: return reference_handle + value;
    case 0x0CU: return reference_handle - value;
    default:    return value; /* 0x2-0x5: absolute; anything else: no known relative meaning */
    }
}

unsigned short dwg_bs_read_t(DWG_BITSTREAM *bs, char *buf, unsigned short buf_size)
{
    unsigned short length = dwg_bs_read_bs(bs);
    unsigned short i;
    unsigned short max_copy = (buf_size > 0U) ? (unsigned short)(buf_size - 1U) : 0U;
    unsigned short copy_len = (length < max_copy) ? length : max_copy;

    for (i = 0U; i < length; i++)
    {
        unsigned char byte = (unsigned char)dwg_bs_read_bits(bs, 8UL);
        if (i < copy_len)
            buf[i] = (char)byte;
    }

    if (buf_size > 0U)
        buf[copy_len] = '\0';

    return length;
}
