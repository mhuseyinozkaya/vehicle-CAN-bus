/*
 * test_readonly_build.cpp - Verifies the SLCAN_READ_ONLY safety
 * interlock.
 *
 * Compiled with -DSLCAN_READ_ONLY=1. The point of that build option is
 * that no sequence of host commands can make the adapter drive the
 * vehicle bus, so this suite tries the sequences that would.
 *
 *   make test      (from the repository root)
 *
 * License: MIT (see LICENSE)
 */

#include <stdio.h>
#include <string>

#include "mocks/Arduino.h"
#include "mocks/mcp_can.h"

#include "config.h"
#include "slcan_protocol.h"

#if SLCAN_READ_ONLY != 1
#error "this suite must be compiled with -DSLCAN_READ_ONLY=1"
#endif

static int g_checks;
static int g_failures;

static void check(bool cond, const char *what)
{
    ++g_checks;
    if (!cond) {
        ++g_failures;
        printf("  FAIL %s\n", what);
    }
}

static std::string send(const std::string &cmd)
{
    Serial.mock_feed(cmd + "\r");
    slcan_poll();
    return Serial.mock_take_tx();
}

int main(void)
{
    printf("SLCAN_READ_ONLY safety interlock\n");
    printf("================================\n");

    Serial.mock_reset();
    g_mock_can.reset();
    mock_set_millis(0);
    slcan_init();

    check(send("S6") == "\r", "bitrate can still be configured");

    /* 'O' opens the channel in normal mode - the one mode that drives
     * the bus. In a read-only build it must be refused outright. */
    check(send("O") == "\a", "'O' (normal mode) is refused");
    check(g_mock_can.mode != MCP_NORMAL, "controller never enters normal mode");

    /* Listening is still allowed - that is the whole point. */
    check(send("L") == "\r", "'L' (listen-only) is allowed");
    check(g_mock_can.mode == MCP_LISTENONLY, "controller is in listen-only mode");

    /* Every transmit form must be rejected. */
    static const char *tx[] = {
        "t1232DEAD", "T18DAF1103112233", "r1234", "R1FFFFFFF0",
    };
    for (size_t i = 0; i < sizeof(tx) / sizeof(tx[0]); ++i) {
        std::string reply = send(tx[i]);
        ++g_checks;
        if (reply != "\a") {
            ++g_failures;
            printf("  FAIL \"%s\" was not refused\n", tx[i]);
        }
    }
    for (int i = 0; i < 16; ++i) slcan_poll();
    check(g_mock_can.tx.empty(), "not a single frame reached the bus");

    /*
     * Loopback stays usable so the adapter can be self-tested without
     * reflashing: the MCP2515 holds TXCAN recessive in that mode, so the
     * frame never reaches the vehicle bus. Closing loopback must not
     * leave the transmit path unlocked.
     */
    send("C");
    check(send("l") == "\r", "'l' (loopback) is allowed - it is bus isolated");
    check(send("t1232DEAD") == "\r", "loopback self-test still works");

    send("C");
    check(send("O") == "\a", "'O' is still refused after a loopback session");
    check(send("L") == "\r", "listen-only reopens");
    check(send("t1232DEAD") == "\a",
          "transmit is refused again once back on the real bus");

    printf("================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
