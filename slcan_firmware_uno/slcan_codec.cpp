/*
 * slcan_codec.cpp - Pure SLCAN (LAWICEL) line codec.
 * See slcan_codec.h. No Arduino dependencies: host-testable.
 *
 * License: MIT (see LICENSE)
 */

#include "slcan_codec.h"

#include <string.h>

int8_t slcan_hex_val(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1; /* invalid - never silently treat as zero */
}

char slcan_hex_chr(uint8_t v)
{
    v &= 0x0F;
    return (char)((v < 10) ? ('0' + v) : ('A' + v - 10));
}

bool slcan_parse_hex(const char *s, uint8_t digits, uint32_t *out)
{
    uint32_t acc = 0;

    for (uint8_t i = 0; i < digits; ++i) {
        int8_t nibble = slcan_hex_val(s[i]);
        if (nibble < 0) return false;
        acc = (acc << 4) | (uint32_t)nibble;
    }
    *out = acc;
    return true;
}

bool slcan_decode(const char *s, can_frame_t *f)
{
    can_frame_t tmp;
    uint8_t     id_digits;
    uint32_t    value;
    size_t      len;
    size_t      expected;

    if (s == NULL || f == NULL) return false;

    switch (s[0]) {
        case 't': tmp.ext = false; tmp.rtr = false; break;
        case 'T': tmp.ext = true;  tmp.rtr = false; break;
        case 'r': tmp.ext = false; tmp.rtr = true;  break;
        case 'R': tmp.ext = true;  tmp.rtr = true;  break;
        default:  return false;
    }

    id_digits = tmp.ext ? 8 : 3;
    len       = strlen(s);

    /* Need at least the type char, the identifier and the DLC digit. */
    if (len < (size_t)(1 + id_digits + 1)) return false;

    if (!slcan_parse_hex(s + 1, id_digits, &value)) return false;
    /* Redundant for the 3-digit case, but keeps extended IDs honest. */
    if (value > (tmp.ext ? CAN_EFF_MASK : CAN_SFF_MASK)) return false;
    tmp.id = value;

    {
        int8_t dlc = slcan_hex_val(s[1 + id_digits]);
        if (dlc < 0 || dlc > CAN_MAX_DLC) return false;
        tmp.dlc = (uint8_t)dlc;
    }

    /*
     * Remote frames carry a DLC but no payload. Reject any trailing
     * garbage instead of quietly ignoring it - a malformed command must
     * never reach the vehicle bus.
     */
    expected = (size_t)(1 + id_digits + 1) + (tmp.rtr ? 0u : (size_t)(2 * tmp.dlc));
    if (len != expected) return false;

    memset(tmp.data, 0, sizeof(tmp.data));
    if (!tmp.rtr) {
        const char *p = s + 1 + id_digits + 1;
        for (uint8_t i = 0; i < tmp.dlc; ++i) {
            uint32_t byte_val;
            if (!slcan_parse_hex(p + (2 * i), 2, &byte_val)) return false;
            tmp.data[i] = (uint8_t)byte_val;
        }
    }

    *f = tmp;
    return true;
}

size_t slcan_encode(char *out, const can_frame_t *f, bool with_ts, uint16_t ts)
{
    size_t   n = 0;
    uint8_t  id_digits;
    uint32_t id;
    uint8_t  dlc;

    if (out == NULL || f == NULL) return 0;

    if (f->ext) {
        out[n++]  = f->rtr ? 'R' : 'T';
        id_digits = 8;
        id        = f->id & CAN_EFF_MASK;
    } else {
        out[n++]  = f->rtr ? 'r' : 't';
        id_digits = 3;
        id        = f->id & CAN_SFF_MASK;
    }

    /* Identifier, most significant nibble first. */
    for (int8_t i = (int8_t)id_digits - 1; i >= 0; --i) {
        out[n + (size_t)i] = slcan_hex_chr((uint8_t)(id & 0x0F));
        id >>= 4;
    }
    n += id_digits;

    dlc = (f->dlc > CAN_MAX_DLC) ? CAN_MAX_DLC : f->dlc;
    out[n++] = slcan_hex_chr(dlc);

    if (!f->rtr) {
        for (uint8_t i = 0; i < dlc; ++i) {
            out[n++] = slcan_hex_chr((uint8_t)(f->data[i] >> 4));
            out[n++] = slcan_hex_chr((uint8_t)(f->data[i] & 0x0F));
        }
    }

    if (with_ts) {
        for (int8_t i = 3; i >= 0; --i) {
            out[n + (size_t)i] = slcan_hex_chr((uint8_t)(ts & 0x0F));
            ts >>= 4;
        }
        n += 4;
    }

    out[n++] = '\r';
    out[n]   = '\0';
    return n;
}
