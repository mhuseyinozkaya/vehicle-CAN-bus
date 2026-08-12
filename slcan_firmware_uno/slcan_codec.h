/*
 * slcan_codec.h - Pure, dependency-free SLCAN (LAWICEL) line codec.
 *
 * This translation unit deliberately contains NO Arduino calls so it can
 * be compiled and unit-tested on a host machine (see test/).
 *
 * License: MIT (see LICENSE)
 */

#ifndef SLCAN_CODEC_H
#define SLCAN_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* CAN definitions                                                     */
/* ------------------------------------------------------------------ */

#define CAN_MAX_DLC  8
#define CAN_SFF_MASK 0x000007FFUL /* 11-bit standard identifier */
#define CAN_EFF_MASK 0x1FFFFFFFUL /* 29-bit extended identifier */

typedef struct {
    uint32_t id;   /* identifier, already masked to 11 or 29 bits */
    uint8_t  dlc;  /* 0..8 */
    bool     ext;  /* true = 29-bit extended frame */
    bool     rtr;  /* true = remote transmission request (no payload)  */
    uint8_t  data[CAN_MAX_DLC];
} can_frame_t;

/*
 * Longest possible SLCAN line we ever emit or accept:
 *   'T' + 8 id + 1 dlc + 16 data + 4 timestamp + CR + NUL = 32
 * Sized with headroom so an over-long host command cannot overflow.
 */
#define SLCAN_LINE_MAX 34

/* ------------------------------------------------------------------ */
/* Hex helpers                                                         */
/* ------------------------------------------------------------------ */

/* Returns 0..15, or -1 when c is not a hex digit. */
int8_t slcan_hex_val(char c);

/* Returns the uppercase ASCII hex digit for the low nibble of v. */
char slcan_hex_chr(uint8_t v);

/*
 * Parses exactly `digits` hex characters starting at s.
 * Returns true on success and writes the value to *out.
 * Returns false if any character is not a valid hex digit.
 */
bool slcan_parse_hex(const char *s, uint8_t digits, uint32_t *out);

/* ------------------------------------------------------------------ */
/* Frame codec                                                         */
/* ------------------------------------------------------------------ */

/*
 * Decodes one transmit command into `f`.
 *
 *   "tIIILDD..DD"        standard data frame
 *   "TIIIIIIIILDD..DD"   extended data frame
 *   "rIIIL"              standard remote frame
 *   "RIIIIIIIIL"         extended remote frame
 *
 * `s` must be NUL-terminated and must NOT contain the trailing CR.
 *
 * The command is validated strictly: unknown type character, wrong
 * length, non-hex characters, DLC > 8 and identifiers that do not fit
 * their frame type are all rejected. Returns false in those cases and
 * leaves `f` untouched.
 */
bool slcan_decode(const char *s, can_frame_t *f);

/*
 * Encodes `f` as an SLCAN line INCLUDING the trailing CR and a NUL
 * terminator, into `out` (must be at least SLCAN_LINE_MAX bytes).
 *
 * When `with_ts` is true a 4-digit millisecond timestamp is appended
 * before the CR, as required by the SLCAN 'Z1' option.
 *
 * Returns the number of characters written, excluding the NUL.
 */
size_t slcan_encode(char *out, const can_frame_t *f, bool with_ts, uint16_t ts);

#ifdef __cplusplus
}
#endif

#endif /* SLCAN_CODEC_H */
