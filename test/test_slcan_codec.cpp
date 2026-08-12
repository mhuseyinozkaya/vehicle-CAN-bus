/*
 * test_slcan_codec.cpp - Host-side unit tests for the SLCAN line codec.
 *
 * The codec is the part of the firmware where a silent bug turns into a
 * malformed frame on a real vehicle bus, so it is deliberately kept free
 * of Arduino dependencies and tested here on the development machine.
 *
 *   make test      (from the repository root)
 *
 * License: MIT (see LICENSE)
 */

#include "../slcan_firmware_uno/slcan_codec.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static void expect_decode_ok(const char *cmd, uint32_t id, uint8_t dlc,
                             bool ext, bool rtr, const uint8_t *data)
{
    can_frame_t f;
    memset(&f, 0xAA, sizeof(f));

    if (!slcan_decode(cmd, &f)) {
        ++g_checks;
        ++g_failures;
        printf("  FAIL decode rejected valid command \"%s\"\n", cmd);
        return;
    }
    CHECK(f.id == id, "\"%s\": id %lu != %lu", cmd, (unsigned long)f.id,
          (unsigned long)id);
    CHECK(f.dlc == dlc, "\"%s\": dlc %u != %u", cmd, f.dlc, dlc);
    CHECK(f.ext == ext, "\"%s\": ext flag wrong", cmd);
    CHECK(f.rtr == rtr, "\"%s\": rtr flag wrong", cmd);
    if (data && !rtr) {
        CHECK(memcmp(f.data, data, dlc) == 0, "\"%s\": payload mismatch", cmd);
    }
}

static void expect_decode_rejected(const char *cmd, const char *why)
{
    can_frame_t f;
    ++g_checks;
    if (slcan_decode(cmd, &f)) {
        ++g_failures;
        printf("  FAIL accepted invalid command \"%s\" (%s)\n", cmd, why);
    }
}

static void expect_encode(const can_frame_t *f, bool ts, uint16_t ts_val,
                          const char *expected)
{
    char   out[SLCAN_LINE_MAX];
    size_t n;

    memset(out, 0x7F, sizeof(out));
    n = slcan_encode(out, f, ts, ts_val);

    ++g_checks;
    if (strcmp(out, expected) != 0) {
        ++g_failures;
        printf("  FAIL encode: got \"%s\" expected \"%s\"\n", out, expected);
        return;
    }
    ++g_checks;
    if (n != strlen(expected)) {
        ++g_failures;
        printf("  FAIL encode length: got %u expected %u\n", (unsigned)n,
               (unsigned)strlen(expected));
    }
}

/* ------------------------------------------------------------------ */

static void test_hex_helpers(void)
{
    printf("hex helpers\n");

    CHECK(slcan_hex_val('0') == 0, "'0'");
    CHECK(slcan_hex_val('9') == 9, "'9'");
    CHECK(slcan_hex_val('A') == 10, "'A'");
    CHECK(slcan_hex_val('F') == 15, "'F'");
    CHECK(slcan_hex_val('a') == 10, "'a'");
    CHECK(slcan_hex_val('f') == 15, "'f'");

    /* Regression: the original firmware returned 0 for invalid input,
     * silently turning "t1G2..." into a valid-looking frame. */
    CHECK(slcan_hex_val('G') == -1, "'G' must be rejected, not folded to 0");
    CHECK(slcan_hex_val('g') == -1, "'g' must be rejected");
    CHECK(slcan_hex_val(' ') == -1, "space must be rejected");
    CHECK(slcan_hex_val('\0') == -1, "NUL must be rejected");

    CHECK(slcan_hex_chr(0) == '0', "0");
    CHECK(slcan_hex_chr(10) == 'A', "10");
    CHECK(slcan_hex_chr(15) == 'F', "15");
    CHECK(slcan_hex_chr(0xF0 | 0x0C) == 'C', "high nibble must be ignored");
}

static void test_decode_standard(void)
{
    const uint8_t obd[8] = {0x02, 0x01, 0x0C, 0, 0, 0, 0, 0};
    const uint8_t two[2] = {0xDE, 0xAD};

    printf("decode - standard frames\n");

    expect_decode_ok("t7DF802010C0000000000", 0x7DF, 8, false, false, obd);
    expect_decode_ok("t1232DEAD", 0x123, 2, false, false, two);
    expect_decode_ok("t0000", 0x000, 0, false, false, NULL);
    expect_decode_ok("t7FF0", 0x7FF, 0, false, false, NULL);

    /* Lowercase hex in the payload must be accepted. */
    expect_decode_ok("t1232dead", 0x123, 2, false, false, two);
}

static void test_decode_extended(void)
{
    const uint8_t d[3] = {0x11, 0x22, 0x33};

    printf("decode - extended frames\n");

    expect_decode_ok("T18DAF1103112233", 0x18DAF110, 3, true, false, d);
    expect_decode_ok("T1FFFFFFF0", 0x1FFFFFFF, 0, true, false, NULL);
    expect_decode_ok("T000000000", 0x00000000, 0, true, false, NULL);
}

static void test_decode_remote(void)
{
    printf("decode - remote frames\n");

    /* Regression: the original firmware had no notion of RTR at all. */
    expect_decode_ok("r1238", 0x123, 8, false, true, NULL);
    expect_decode_ok("R18DAF1104", 0x18DAF110, 4, true, true, NULL);

    /* A remote frame carries a DLC but never a payload. */
    expect_decode_rejected("r1238DEADBEEF", "RTR frame with payload");
}

static void test_decode_rejects_garbage(void)
{
    printf("decode - malformed input is rejected\n");

    expect_decode_rejected("", "empty");
    expect_decode_rejected("x1232DEAD", "unknown command letter");
    expect_decode_rejected("t12", "truncated, no DLC");
    expect_decode_rejected("t123", "no DLC digit");
    expect_decode_rejected("t1239", "DLC 9 exceeds 8");
    expect_decode_rejected("t123F", "DLC F exceeds 8");
    expect_decode_rejected("t1232DE", "payload shorter than DLC");
    expect_decode_rejected("t1232DEADBE", "payload longer than DLC");
    expect_decode_rejected("t12G2DEAD", "non-hex in identifier");
    expect_decode_rejected("t1232DEZD", "non-hex in payload");
    expect_decode_rejected("T18DAF1103", "extended, payload shorter than DLC");
    expect_decode_rejected("T18DAF11", "extended id too short");

    /*
     * Regression: an out-of-range DLC used to be detected only *after*
     * the caller had already armed the send flag, so a garbage frame was
     * still pushed onto the vehicle bus.
     */
    expect_decode_rejected("t123900000000000000000", "DLC 9 with 9 bytes");
}

static void test_encode(void)
{
    can_frame_t f;

    printf("encode\n");

    memset(&f, 0, sizeof(f));
    f.id = 0x7E8; f.dlc = 8; f.ext = false; f.rtr = false;
    f.data[0] = 0x03; f.data[1] = 0x41; f.data[2] = 0x0C;
    f.data[3] = 0x1A; f.data[4] = 0xF8;
    expect_encode(&f, false, 0, "t7E8803410C1AF8000000\r");

    memset(&f, 0, sizeof(f));
    f.id = 0x123; f.dlc = 0;
    expect_encode(&f, false, 0, "t1230\r");

    /* Identifier must be zero padded to three nibbles. */
    memset(&f, 0, sizeof(f));
    f.id = 0x7; f.dlc = 1; f.data[0] = 0xFF;
    expect_encode(&f, false, 0, "t0071FF\r");

    /* Extended identifiers use eight nibbles and an uppercase 'T'. */
    memset(&f, 0, sizeof(f));
    f.id = 0x18DAF110; f.dlc = 3; f.ext = true;
    f.data[0] = 0x11; f.data[1] = 0x22; f.data[2] = 0x33;
    expect_encode(&f, false, 0, "T18DAF1103112233\r");

    /* Remote frames emit the DLC but no payload. */
    memset(&f, 0, sizeof(f));
    f.id = 0x123; f.dlc = 8; f.rtr = true;
    expect_encode(&f, false, 0, "r1238\r");

    memset(&f, 0, sizeof(f));
    f.id = 0x1FFFFFFF; f.dlc = 2; f.ext = true; f.rtr = true;
    expect_encode(&f, false, 0, "R1FFFFFFF2\r");

    /* Timestamps are four hex digits, inserted before the CR. */
    memset(&f, 0, sizeof(f));
    f.id = 0x123; f.dlc = 1; f.data[0] = 0xAB;
    expect_encode(&f, true, 0x04D2, "t1231AB04D2\r");
    expect_encode(&f, true, 0x0000, "t1231AB0000\r");
    expect_encode(&f, true, 0xEA5F, "t1231ABEA5F\r");
}

static void test_round_trip(void)
{
    static const char *cases[] = {
        "t0000",           "t7FF8DEADBEEFCAFEBABE",
        "t1232DEAD",       "T000000000",
        "T1FFFFFFF8AABBCCDDEEFF0011",
        "T18DAF1103112233", "r1238", "R1FFFFFFF0",
    };
    char        out[SLCAN_LINE_MAX];
    can_frame_t f;

    printf("round trip decode -> encode\n");

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ++g_checks;
        if (!slcan_decode(cases[i], &f)) {
            ++g_failures;
            printf("  FAIL round trip: \"%s\" did not decode\n", cases[i]);
            continue;
        }
        slcan_encode(out, &f, false, 0);
        /* Strip the CR the encoder appends before comparing. */
        out[strlen(out) - 1] = '\0';
        ++g_checks;
        if (strcmp(out, cases[i]) != 0) {
            ++g_failures;
            printf("  FAIL round trip: \"%s\" -> \"%s\"\n", cases[i], out);
        }
    }
}

static void test_buffer_bounds(void)
{
    char        out[SLCAN_LINE_MAX];
    can_frame_t f;
    size_t      n;

    printf("buffer bounds\n");

    /* Worst case: extended, remote-free, 8 data bytes, with timestamp. */
    memset(&f, 0, sizeof(f));
    f.id  = 0x1FFFFFFF;
    f.dlc = 8;
    f.ext = true;
    memset(f.data, 0xFF, sizeof(f.data));

    n = slcan_encode(out, &f, true, 0xFFFF);
    CHECK(n + 1 <= SLCAN_LINE_MAX, "worst case line (%u+NUL) exceeds buffer %u",
          (unsigned)n, (unsigned)SLCAN_LINE_MAX);
    CHECK(out[n] == '\0', "encoder must NUL terminate");
    CHECK(out[n - 1] == '\r', "encoder must append CR");

    /* An over-long host command must be rejected, not truncated. */
    expect_decode_rejected("T1FFFFFFF8FFFFFFFFFFFFFFFFFFFFFFFFFFFF",
                           "payload longer than DLC allows");
}

int main(void)
{
    printf("SLCAN codec test suite\n");
    printf("======================\n");

    test_hex_helpers();
    test_decode_standard();
    test_decode_extended();
    test_decode_remote();
    test_decode_rejects_garbage();
    test_encode();
    test_round_trip();
    test_buffer_bounds();

    printf("======================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
