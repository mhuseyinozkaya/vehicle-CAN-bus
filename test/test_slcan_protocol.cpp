/*
 * test_slcan_protocol.cpp - Integration tests for the SLCAN command
 * interpreter, run against a mock Arduino/MCP2515 HAL.
 *
 * Every test named "regression:" pins down a bug that existed in the
 * original single-file firmware.
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
#include "slcan_codec.h"
#include "slcan_protocol.h"

static int g_checks;
static int g_failures;

static void fail(const char *what)
{
    ++g_failures;
    printf("  FAIL %s\n", what);
}

static void check(bool cond, const char *what)
{
    ++g_checks;
    if (!cond) fail(what);
}

/* Make CR and BEL visible in failure output. */
static std::string escape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r')      out += "<CR>";
        else if (s[i] == '\a') out += "<BEL>";
        else if (s[i] == '\n') out += "<LF>";
        else                   out += s[i];
    }
    return out;
}

static void check_str(const std::string &got, const std::string &want,
                      const char *what)
{
    ++g_checks;
    if (got == want) return;
    ++g_failures;
    printf("  FAIL %s\n", what);
    printf("       got  \"%s\"\n", escape(got).c_str());
    printf("       want \"%s\"\n", escape(want).c_str());
}

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

/* Feed a command (CR appended automatically) and run one poll cycle. */
static std::string send(const std::string &cmd)
{
    Serial.mock_feed(cmd + "\r");
    slcan_poll();
    return Serial.mock_take_tx();
}

/* Run one poll cycle with no new input. */
static std::string poll(void)
{
    slcan_poll();
    return Serial.mock_take_tx();
}

static void reset_all(void)
{
    Serial.mock_reset();
    g_mock_can.reset();
    mock_set_millis(0);
    slcan_init();
}

/* Bring the adapter to "configured and open in normal mode". */
static void open_normal(void)
{
    reset_all();
    send("S6");
    send("O");
}

static void push_rx(uint32_t id, uint8_t len, const uint8_t *data, bool ext,
                    bool rtr)
{
    MockCanFrame f{};
    f.id  = id;
    f.ext = ext ? 1 : 0;
    f.rtr = rtr ? 1 : 0;
    f.len = len;
    if (data) memcpy(f.data, data, len);
    g_mock_can.rx.push_back(f);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_open_requires_bitrate(void)
{
    printf("regression: channel cannot be opened before the bitrate is set\n");

    reset_all();
    /*
     * The original firmware ran setMode() on a controller that had never
     * been through begin(), so 'O' before 'S' left the adapter in an
     * undefined state that looked open but moved no data.
     */
    check_str(send("O"), "\a", "'O' before 'S' must be rejected");
    check(!g_mock_can.begin_calls, "no SPI traffic before a bitrate is set");

    check_str(send("S6"), "\r", "'S6' is acknowledged");
    check_str(send("O"), "\r", "'O' after 'S6' is acknowledged");
    check(g_mock_can.mode == MCP_NORMAL, "controller is in normal mode");
}

static void test_bitrate_validation(void)
{
    printf("bitrate command validation\n");

    reset_all();
    check_str(send("S0"), "\r", "S0 = 10 kbit/s");
    check_str(send("S8"), "\r", "S8 = 1 Mbit/s");
    check_str(send("S9"), "\a", "S9 is out of range");
    check_str(send("S7"), "\a", "S7 (800 kbit/s) is unsupported by the MCP2515");
    check_str(send("SX"), "\a", "non-hex bitrate digit");
    check_str(send("S"), "\a", "missing bitrate digit");
    check_str(send("S66"), "\a", "trailing garbage");

    /* Changing the bitrate under a live channel makes no sense. */
    send("S6");
    send("O");
    check_str(send("S5"), "\a", "bitrate change is refused while open");
}

static void test_close_stops_driving_the_bus(void)
{
    printf("regression: closing the channel must silence the controller\n");

    open_normal();
    /*
     * The original firmware only flipped a software flag on 'C'. The
     * MCP2515 stayed in normal mode and kept acknowledging every frame
     * on the vehicle bus - an invisible way to disturb a bus you believe
     * you are merely observing.
     */
    check_str(send("C"), "\r", "'C' is acknowledged");
    check(g_mock_can.mode == MCP_CONFIG,
          "controller is parked in configuration mode after close");

    /* slcand sends 'C' unconditionally at start-up; it must not error. */
    reset_all();
    check_str(send("C"), "\r", "'C' on an already closed channel is acknowledged");
}

static void test_listen_only_never_transmits(void)
{
    printf("listen-only mode refuses every transmit\n");

    reset_all();
    send("S6");
    check_str(send("L"), "\r", "'L' opens the channel");
    check(g_mock_can.mode == MCP_LISTENONLY, "controller is in listen-only mode");

    check_str(send("t1232DEAD"), "\a", "transmit is refused in listen-only mode");
    poll();
    check(g_mock_can.tx.empty(), "nothing reached the bus in listen-only mode");
}

static void test_transmit_while_closed(void)
{
    printf("transmit is refused while the channel is closed\n");

    reset_all();
    send("S6");
    check_str(send("t1232DEAD"), "\a", "transmit before open is refused");
    poll();
    check(g_mock_can.tx.empty(), "nothing reached the bus");
}

static void test_transmit_happy_path(void)
{
    printf("transmit - well formed frames reach the bus\n");

    open_normal();
    check_str(send("t7DF802010C0000000000"), "\r", "OBD-II query accepted");
    poll(); /* the queue is serviced on the following pass */

    check(g_mock_can.tx.size() == 1, "exactly one frame was transmitted");
    if (g_mock_can.tx.size() == 1) {
        const MockCanFrame &f = g_mock_can.tx[0];
        check(f.id == 0x7DF, "identifier 0x7DF");
        check(f.ext == 0, "standard frame");
        check(f.rtr == 0, "data frame");
        check(f.len == 8, "DLC 8");
        check(f.data[0] == 0x02 && f.data[1] == 0x01 && f.data[2] == 0x0C,
              "payload preserved");
    }

    /* Extended identifiers must keep their extended flag on the way out. */
    g_mock_can.tx.clear();
    check_str(send("T18DAF1103112233"), "\r", "extended frame accepted");
    poll();
    check(g_mock_can.tx.size() == 1, "extended frame transmitted");
    if (g_mock_can.tx.size() == 1) {
        check(g_mock_can.tx[0].ext == 1, "extended flag preserved");
        check(g_mock_can.tx[0].id == 0x18DAF110, "extended identifier preserved");
    }

    /* Remote frames. */
    g_mock_can.tx.clear();
    check_str(send("r1234"), "\r", "remote frame accepted");
    poll();
    check(g_mock_can.tx.size() == 1, "remote frame transmitted");
    if (g_mock_can.tx.size() == 1) {
        check(g_mock_can.tx[0].rtr == 1, "RTR flag set");
        check(g_mock_can.tx[0].len == 4, "remote frame keeps its DLC");
    }
}

static void test_malformed_transmit_never_reaches_the_bus(void)
{
    printf("regression: a rejected command must not be transmitted anyway\n");

    /*
     * The original code called serialcan_to_frame() and then set
     * CAN_SEND_FLAG unconditionally - even when the parser had bailed
     * out on a DLC above 8. The half-parsed frame was still pushed onto
     * the vehicle bus on the next loop iteration.
     */
    static const char *bad[] = {
        "t1239DEADBEEF",   /* DLC 9                         */
        "t1232DE",         /* payload shorter than the DLC  */
        "t1232DEADBE",     /* payload longer than the DLC   */
        "t12G2DEAD",       /* non-hex identifier            */
        "t1232DEZD",       /* non-hex payload               */
        "T18DAF11",        /* truncated extended identifier */
        "r1238DEADBEEF",   /* remote frame with a payload   */
        "q1232DEAD",       /* unknown command letter        */
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        open_normal();
        std::string reply = send(bad[i]);
        poll();
        poll();
        ++g_checks;
        if (reply != "\a") {
            fail((std::string("\"") + bad[i] + "\" was not rejected").c_str());
        }
        ++g_checks;
        if (!g_mock_can.tx.empty()) {
            fail((std::string("\"") + bad[i] + "\" still reached the bus").c_str());
        }
    }
}

static void test_receive_encoding(void)
{
    printf("receive - frames are encoded correctly for the host\n");

    const uint8_t payload[8] = {0x03, 0x41, 0x0C, 0x1A, 0xF8, 0, 0, 0};

    open_normal();
    push_rx(0x7E8, 8, payload, false, false);
    check_str(poll(), "t7E8803410C1AF8000000\r", "standard frame");

    /*
     * Regression: extended frames used to be emitted with a lowercase
     * 't' and a three digit identifier, so SocketCAN saw a completely
     * different (and wrong) CAN ID.
     */
    const uint8_t d3[3] = {0x11, 0x22, 0x33};
    push_rx(0x18DAF110, 3, d3, true, false);
    check_str(poll(), "T18DAF1103112233\r", "extended frame uses 'T' and 8 digits");

    /* Regression: remote frames were emitted as data frames with junk. */
    push_rx(0x123, 4, NULL, false, true);
    check_str(poll(), "r1234\r", "remote frame uses 'r' and carries no payload");

    push_rx(0x1FFFFFFF, 0, NULL, true, true);
    check_str(poll(), "R1FFFFFFF0\r", "extended remote frame");
}

static void test_receive_drains_both_buffers(void)
{
    printf("regression: more than one frame is drained per loop pass\n");

    const uint8_t d[1] = {0xAA};

    open_normal();
    /*
     * The MCP2515 has two receive buffers. Reading only one per loop -
     * as the original did - leaves /INT asserted and drops frames as
     * soon as the bus gets busy.
     */
    push_rx(0x100, 1, d, false, false);
    push_rx(0x101, 1, d, false, false);
    check_str(poll(), "t1001AA\rt1011AA\r", "both pending frames drained in one pass");
}

static void test_receive_is_silent_while_closed(void)
{
    printf("no frames are reported while the channel is closed\n");

    const uint8_t d[1] = {0xAA};

    reset_all();
    send("S6");
    push_rx(0x100, 1, d, false, false);
    check_str(poll(), "", "closed channel emits nothing");
}

static void test_partial_command_does_not_block(void)
{
    printf("regression: a partial command must not stall the firmware\n");

    const uint8_t d[1] = {0xAA};

    open_normal();

    /*
     * The original read_serial_port() busy-waited for a CR. A command
     * split across two USB packets froze loop() completely, and every
     * CAN frame that arrived in the meantime was lost.
     */
    Serial.mock_feed("t123");
    push_rx(0x200, 1, d, false, false);

    std::string out = poll();
    check_str(out, "t2001AA\r",
              "CAN reception continues while a command is still incomplete");

    Serial.mock_feed("2DEAD\r");
    check_str(poll(), "\r", "the command completes on a later pass");
    poll();
    check(g_mock_can.tx.size() == 1, "the reassembled frame was transmitted");
    if (g_mock_can.tx.size() == 1) {
        check(g_mock_can.tx[0].id == 0x123 && g_mock_can.tx[0].len == 2,
              "reassembled frame is correct");
    }
}

static void test_line_handling_edge_cases(void)
{
    printf("line handling edge cases\n");

    open_normal();

    /* CR LF line endings and stray empty lines must be tolerated. */
    Serial.mock_feed("t1232DEAD\r\n");
    check_str(poll(), "\r", "CR LF is accepted");

    Serial.mock_feed("\r\r\r");
    check_str(poll(), "", "empty lines are ignored, not answered with errors");

    /*
     * An over-long line is rejected once and must not corrupt the next
     * one. Input is consumed in bounded chunks per pass (so a fast host
     * cannot starve the CAN side), hence the loop.
     */
    Serial.mock_feed(std::string(200, 'A') + "\r");
    std::string reply;
    for (int i = 0; i < 8; ++i) reply += poll();
    check_str(reply, "\a", "over-long line is rejected exactly once");
    check_str(send("t1232DEAD"), "\r", "the parser recovers on the next line");

    /* Several commands arriving in one write - what slcand actually does. */
    reset_all();
    Serial.mock_feed("C\rS6\r");
    check_str(poll(), "\r\r", "batched commands are all answered");
    check_str(send("O"), "\r", "channel opens after the batch");
}

static void test_status_command(void)
{
    printf("'F' status command\n");

    open_normal();
    check_str(send("F"), "F00\r", "no faults after a clean open");

    g_mock_can.error_state = CAN_CTRLERROR;
    check_str(send("F"), "F80\r", "controller error raises the bus-error bit");

    g_mock_can.error_state = CAN_OK;
    check_str(send("F"), "F00\r", "status bits are cleared once read");

    check_str(send("FF"), "\a", "trailing garbage on 'F' is rejected");
}

static void test_version_commands(void)
{
    printf("version and serial-number commands\n");

    reset_all();
    check_str(send("V"), "V1020\r", "'V' reports hardware and software version");
    check_str(send("v"), "v20\r", "'v' reports the software version");
    check_str(send("N"), "N0001\r", "'N' reports the serial number");
    check_str(send("?"), "\a", "an unknown command is rejected");
}

static void test_timestamps(void)
{
    printf("'Z' timestamp option\n");

    const uint8_t d[1] = {0xAB};

    open_normal();
    check_str(send("Z1"), "\r", "timestamps enabled");

    mock_set_millis(0x04D2);
    push_rx(0x123, 1, d, false, false);
    check_str(poll(), "t1231AB04D2\r", "timestamp appended before the CR");

    /* The SLCAN timestamp wraps at 60000 ms. */
    mock_set_millis(60000 + 0x10);
    push_rx(0x123, 1, d, false, false);
    check_str(poll(), "t1231AB0010\r", "timestamp wraps at 60 s");

    check_str(send("Z0"), "\r", "timestamps disabled");
    push_rx(0x123, 1, d, false, false);
    check_str(poll(), "t1231AB\r", "no timestamp once disabled");

    check_str(send("Z2"), "\a", "invalid timestamp argument rejected");
}

static void test_acceptance_filter_inversion(void)
{
    printf("regression: SLCAN acceptance mask is inverted for the MCP2515\n");

    reset_all();
    send("S6");

    /*
     * SLCAN inherits SJA1000 semantics: a mask bit of 1 means "don't
     * care". The MCP2515 uses the opposite convention, so the mask has
     * to be inverted or the filter keeps exactly the frames the user
     * asked to drop.
     */
    check_str(send("M000007E8"), "\r", "acceptance code accepted");
    check_str(send("m00000000"), "\r", "acceptance mask accepted");
    check_str(send("O"), "\r", "channel opens with a filter installed");

    check(g_mock_can.last_begin_idmode == MCP_STDEXT,
          "filtering requires the driver to leave 'receive any' mode");
    check(g_mock_can.masks.size() == 2, "both receive-buffer masks programmed");
    check(g_mock_can.filters.size() == 6, "all six filters programmed");
    if (!g_mock_can.masks.empty()) {
        check(g_mock_can.masks[0] == 0x7FF,
              "SLCAN mask 0x00000000 becomes MCP2515 mask 0x7FF");
    }
    if (!g_mock_can.filters.empty()) {
        check(g_mock_can.filters[0] == 0x7E8, "acceptance code programmed as-is");
    }

    check_str(send("m00000000"), "\a", "filters cannot be changed while open");
}

static void test_transmit_queue_backpressure(void)
{
    printf("transmit queue is bounded and reports overflow\n");

    open_normal();
    g_mock_can.send_status = CAN_GETTXBFTIMEOUT; /* controller never frees a buffer */

    int accepted = 0;
    for (int i = 0; i < 32; ++i) {
        Serial.mock_feed("t1231AA\r");
        std::string r = Serial.mock_take_tx();
        slcan_poll();
        r += Serial.mock_take_tx();
        if (r.find('\r') != std::string::npos) ++accepted;
    }
    check(accepted > 0 && accepted < 32,
          "the queue accepts a bounded number of frames, then pushes back");

    /* Recovery: once the controller drains, transmission resumes. */
    g_mock_can.send_status = CAN_OK;
    for (int i = 0; i < 64; ++i) slcan_poll();
    check(!g_mock_can.tx.empty(), "queued frames are sent once the bus recovers");

    check_str(send("t1231BB"), "\r", "new frames are accepted again");
}

static void test_serial_backpressure_does_not_block(void)
{
    printf("regression: a stalled host must not block the receive path\n");

    const uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    open_normal();
    /*
     * If the host stops reading, Serial.write() blocks on real hardware.
     * That stalls the CAN drain loop and silently loses frames with no
     * indication. Instead we drop the frame and latch an overrun bit.
     */
    Serial.write_space = 0;
    push_rx(0x123, 8, d, false, false);
    check_str(poll(), "", "the frame is dropped rather than blocking");

    Serial.write_space = 64;
    std::string status = send("F");
    check(status.size() == 4 && status[0] == 'F',
          "status is reported after the drop");
    check(status[1] == '0' && (status[2] == '9'),
          "data-overrun and RX-full bits are latched");
}

/* ------------------------------------------------------------------ */
/* Bitrate scanner                                                     */
/* ------------------------------------------------------------------ */

#if SLCAN_AUTODETECT

/*
 * Drives one scan step: optionally injects traffic, lets the firmware
 * drain it, then advances the clock past the dwell time so the step is
 * evaluated. Returns whatever the firmware emitted.
 */
static std::string scan_step(uint32_t &now, int frames_to_inject, bool clean)
{
    const uint8_t d[2] = {0xAA, 0x55};

    for (int i = 0; i < frames_to_inject; ++i) {
        push_rx(0x100 + (uint32_t)i, 2, d, false, false);
    }
    g_mock_can.error_state = clean ? CAN_OK : CAN_CTRLERROR;

    std::string out = poll(); /* drains the injected frames */
    now += SLCAN_AUTODETECT_DWELL_MS;
    mock_set_millis(now);
    out += poll(); /* evaluates the step and moves on */
    return out;
}

static void test_autodetect_finds_the_bitrate(void)
{
    printf("bitrate scanner finds the live network\n");

    uint32_t now = 1000;
    reset_all();
    mock_set_millis(now);

    check_str(send("B"), "", "'B' replies only when the scan finishes");

    /*
     * Candidate order is 500k, 125k, 250k, 1M, 100k, 50k, 20k, 10k.
     * Only the first candidate carries clean traffic here.
     */
    std::string out;
    out += scan_step(now, 6, true);   /* S6 - 500 kbit/s, the real one */
    for (int i = 1; i < 8; ++i) {
        out += scan_step(now, 0, false); /* wrong bitrate: no frames, errors */
    }
    check_str(out, "B6\r", "scan reports S6");

    /* Probing must never leave listen-only mode. */
    check(g_mock_can.mode == MCP_CONFIG,
          "controller is parked in configuration mode after the scan");

    /* The detected bitrate is left configured, so 'O' just works. */
    check_str(send("O"), "\r", "channel opens at the detected bitrate");
    check(g_mock_can.last_speed == CAN_500KBPS, "opened at 500 kbit/s");
}

static void test_autodetect_prefers_the_clean_candidate(void)
{
    printf("bitrate scanner prefers the candidate without controller errors\n");

    uint32_t now = 1000;
    reset_all();
    mock_set_millis(now);
    send("B");

    std::string out;
    /*
     * A wrong bitrate can decode the odd frame by accident while piling
     * up receive errors. A clean candidate with fewer frames must still
     * win - error state is the more trustworthy signal.
     */
    out += scan_step(now, 9, false);  /* S6: noisy, many frames  */
    out += scan_step(now, 3, true);   /* S4: clean, fewer frames */
    for (int i = 2; i < 8; ++i) out += scan_step(now, 0, false);

    check_str(out, "B4\r", "the clean candidate wins");
}

static void test_autodetect_reports_failure(void)
{
    printf("bitrate scanner reports failure on a silent bus\n");

    uint32_t now = 1000;
    reset_all();
    mock_set_millis(now);
    send("B");

    std::string out;
    for (int i = 0; i < 8; ++i) out += scan_step(now, 0, true);
    check_str(out, "\a", "a silent bus is reported as a failure");

    check_str(send("O"), "\a", "the channel stays unconfigured after a failed scan");

    /* A single stray frame is below the threshold and must not count. */
    now = 1000;
    reset_all();
    mock_set_millis(now);
    send("B");
    out.clear();
    out += scan_step(now, 1, true);
    for (int i = 1; i < 8; ++i) out += scan_step(now, 0, true);
    check_str(out, "\a", "one lone frame is not enough to declare a match");
}

static void test_autodetect_guards(void)
{
    printf("bitrate scanner guards\n");

    reset_all();
    send("S6");
    send("O");
    check_str(send("B"), "\a", "scanning is refused while the channel is open");

    uint32_t now = 1000;
    reset_all();
    mock_set_millis(now);
    send("B");

    /* Other commands are refused, and must not disturb the scan. */
    check_str(send("S6"), "\a", "commands are refused during a scan");
    check_str(send("t1232DEAD"), "\a", "transmit is refused during a scan");
    check_str(send("V"), "\a", "even harmless queries are refused during a scan");

    /* 'C' is the escape hatch. */
    check_str(send("C"), "\r", "'C' aborts the scan");
    check(g_mock_can.mode == MCP_CONFIG, "aborting parks the controller");
    check_str(send("V"), "V1020\r", "normal command handling resumes after abort");
    check_str(send("O"), "\a",
              "an aborted scan leaves no usable bitrate behind");

    check_str(send("BB"), "\a", "trailing garbage on 'B' is rejected");
}

static void test_autodetect_never_drives_the_bus(void)
{
    printf("regression: probing an unknown bus must stay listen-only\n");

    uint32_t now = 1000;
    reset_all();
    mock_set_millis(now);
    send("B");

    /*
     * Guessing at the wrong bit timing while driving the bus is the
     * fastest way to push a vehicle's real modules into bus-off. Every
     * probe step must therefore stay in listen-only mode.
     */
    for (int i = 0; i < 8; ++i) {
        ++g_checks;
        if (g_mock_can.mode != MCP_LISTENONLY) {
            fail("probe step left listen-only mode");
        }
        scan_step(now, 0, true);
    }
    check(g_mock_can.tx.empty(), "the scanner never transmitted anything");
}

#else /* !SLCAN_AUTODETECT */

static void test_autodetect_finds_the_bitrate(void)
{
    printf("bitrate scanner disabled at compile time\n");
    reset_all();
    check_str(send("B"), "\a", "'B' is refused when the scanner is compiled out");
}

static void test_autodetect_prefers_the_clean_candidate(void) {}
static void test_autodetect_reports_failure(void) {}
static void test_autodetect_guards(void) {}
static void test_autodetect_never_drives_the_bus(void) {}

#endif /* SLCAN_AUTODETECT */

int main(void)
{
    printf("SLCAN protocol integration tests\n");
    printf("================================\n");

    test_open_requires_bitrate();
    test_bitrate_validation();
    test_close_stops_driving_the_bus();
    test_listen_only_never_transmits();
    test_transmit_while_closed();
    test_transmit_happy_path();
    test_malformed_transmit_never_reaches_the_bus();
    test_receive_encoding();
    test_receive_drains_both_buffers();
    test_receive_is_silent_while_closed();
    test_partial_command_does_not_block();
    test_line_handling_edge_cases();
    test_status_command();
    test_version_commands();
    test_timestamps();
    test_acceptance_filter_inversion();
    test_transmit_queue_backpressure();
    test_serial_backpressure_does_not_block();
    test_autodetect_finds_the_bitrate();
    test_autodetect_prefers_the_clean_candidate();
    test_autodetect_reports_failure();
    test_autodetect_guards();
    test_autodetect_never_drives_the_bus();

    printf("================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
