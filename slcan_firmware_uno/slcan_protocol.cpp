/*
 * slcan_protocol.cpp - SLCAN command interpreter and frame pump.
 *
 * Design rules enforced here:
 *   1. Nothing blocks. Every function returns promptly so the CAN
 *      receive path is never starved by the serial path or vice versa.
 *   2. A command is executed only after it has been fully validated.
 *      A malformed line is rejected, never partially applied.
 *   3. The bus is only ever driven after the host has explicitly opened
 *      the channel in a mode that allows it.
 *
 * License: MIT (see LICENSE)
 */

#include <Arduino.h>
#include <string.h>

#include "can_iface.h"
#include "config.h"
#include "slcan_codec.h"
#include "slcan_protocol.h"

/* SLCAN wire-level responses. */
#define SLCAN_CR  '\r'
#define SLCAN_BEL '\a'

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static char    s_line[SLCAN_LINE_MAX];
static uint8_t s_line_len;
static bool    s_line_overflow;

static uint8_t s_status_flags;
static bool    s_timestamp_on = (SLCAN_TIMESTAMP_DEFAULT != 0);

/* Outgoing (host -> CAN bus) frame queue. */
static can_frame_t s_txq[SLCAN_TX_QUEUE_LEN];
static uint8_t     s_tx_head;
static uint8_t     s_tx_tail;
static uint8_t     s_tx_count;
static uint8_t     s_tx_retries;

/*
 * How many times a single frame may fail to reach a hardware transmit
 * buffer before it is discarded. Without this, an unterminated or
 * disconnected bus (no ACK -> the controller retries forever) would wedge
 * the queue permanently and the adapter would appear to hang.
 */
#define SLCAN_TX_MAX_RETRIES 32

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static inline void respond_ok(void)  { Serial.write((uint8_t)SLCAN_CR); }
static inline void respond_err(void) { Serial.write((uint8_t)SLCAN_BEL); }

static void tx_queue_reset(void)
{
    s_tx_head    = 0;
    s_tx_tail    = 0;
    s_tx_count   = 0;
    s_tx_retries = 0;
}

static bool tx_queue_push(const can_frame_t *f)
{
    if (s_tx_count >= SLCAN_TX_QUEUE_LEN) return false;
    s_txq[s_tx_tail] = *f;
    s_tx_tail        = (uint8_t)((s_tx_tail + 1) % SLCAN_TX_QUEUE_LEN);
    ++s_tx_count;
    return true;
}

static void tx_queue_pop(void)
{
    if (s_tx_count == 0) return;
    s_tx_head    = (uint8_t)((s_tx_head + 1) % SLCAN_TX_QUEUE_LEN);
    --s_tx_count;
    s_tx_retries = 0;
}

/* Free-running millisecond timestamp, 0..59999, as the SLCAN spec wants. */
static uint16_t slcan_timestamp(void)
{
    return (uint16_t)(millis() % 60000UL);
}

/* ------------------------------------------------------------------ */
/* Command handlers                                                    */
/* ------------------------------------------------------------------ */

static void cmd_set_bitrate(const char *line)
{
    int8_t code;

    /* Changing the bitrate under a live channel is not meaningful. */
    if (can_iface_is_open())  { respond_err(); return; }
    if (strlen(line) != 2)    { respond_err(); return; }

    code = slcan_hex_val(line[1]);
    if (code < 0 || code > 8) { respond_err(); return; }

    if (can_iface_configure((uint8_t)code)) respond_ok();
    else                                    respond_err();
}

static void cmd_open(const char *line, can_mode_t mode)
{
    if (strlen(line) != 1)         { respond_err(); return; }
    if (can_iface_is_open())       { respond_err(); return; }
    if (!can_iface_is_configured()){ respond_err(); return; }

    if (can_iface_open(mode)) {
        tx_queue_reset();
        s_status_flags = 0;
        respond_ok();
    } else {
        respond_err();
    }
}

static void cmd_close(const char *line)
{
    if (strlen(line) != 1) { respond_err(); return; }

    can_iface_close();
    tx_queue_reset();
    /*
     * Always acknowledge, even if the channel was already closed:
     * slcand unconditionally sends 'C' before configuring the link and
     * treats a NACK there as a fatal error.
     */
    respond_ok();
}

static void cmd_transmit(const char *line)
{
    can_frame_t frame;

    if (!can_iface_is_open())      { respond_err(); return; }
    if (!can_iface_can_transmit()) { respond_err(); return; }
    if (!slcan_decode(line, &frame)) { respond_err(); return; }

    if (!tx_queue_push(&frame)) {
        s_status_flags |= SLCAN_FLAG_TX_FIFO_FULL;
        respond_err();
        return;
    }
    respond_ok();
}

static void cmd_status(const char *line)
{
    uint8_t flags;

    if (strlen(line) != 1) { respond_err(); return; }

    flags = s_status_flags;
    if (can_iface_has_error()) flags |= SLCAN_FLAG_BUS_ERROR;
    if (s_tx_count >= SLCAN_TX_QUEUE_LEN) flags |= SLCAN_FLAG_TX_FIFO_FULL;

    Serial.write((uint8_t)'F');
    Serial.write((uint8_t)slcan_hex_chr((uint8_t)(flags >> 4)));
    Serial.write((uint8_t)slcan_hex_chr((uint8_t)(flags & 0x0F)));
    Serial.write((uint8_t)SLCAN_CR);

    /* Reading the status register clears the latched bits. */
    s_status_flags = 0;
}

/*
 * 'M' (acceptance code) and 'm' (acceptance mask) are two halves of one
 * setting, so both are remembered here and pushed down together. The
 * SJA1000 -> MCP2515 mask inversion happens inside can_iface.
 */
static uint32_t s_acc_code = 0;
static uint32_t s_acc_mask = 0xFFFFFFFFUL; /* all bits "don't care" */

static void cmd_filter(const char *line, bool is_mask)
{
    uint32_t value;

    /* Acceptance filters may only be programmed on a closed channel. */
    if (can_iface_is_open())                   { respond_err(); return; }
    if (strlen(line) != 9)                     { respond_err(); return; }
    if (!slcan_parse_hex(line + 1, 8, &value)) { respond_err(); return; }

    if (is_mask) s_acc_mask = value;
    else         s_acc_code = value;

    can_iface_set_filter(s_acc_code, s_acc_mask);
    respond_ok();
}

static void cmd_timestamp(const char *line)
{
    if (strlen(line) != 2) { respond_err(); return; }

    if (line[1] == '0')      s_timestamp_on = false;
    else if (line[1] == '1') s_timestamp_on = true;
    else                     { respond_err(); return; }

    respond_ok();
}

/* ------------------------------------------------------------------ */
/* Bitrate scanner ('B')                                               */
/* ------------------------------------------------------------------ */

#if SLCAN_AUTODETECT

/*
 * Candidate bitrates, most likely first. 500 kbit/s (HS-CAN) and
 * 125 kbit/s (Ford MS-CAN and similar body networks) cover the vast
 * majority of OBD-II ports, so a scan usually settles early.
 */
static const uint8_t k_probe_order[] = {6, 4, 5, 8, 3, 2, 1, 0};
#define AUTODETECT_STEPS ((uint8_t)(sizeof(k_probe_order)))

static struct {
    bool     active;
    uint8_t  step;
    uint32_t step_started;
    uint8_t  frames;      /* frames seen at the current candidate */
    uint16_t best_score;
    uint8_t  best_code;
} s_scan;

static void autodetect_reset(void)
{
    s_scan.active     = false;
    s_scan.step       = 0;
    s_scan.frames     = 0;
    s_scan.best_score = 0;
    s_scan.best_code  = 0;
}

static bool autodetect_active(void) { return s_scan.active; }

static void autodetect_start_step(void)
{
    s_scan.frames       = 0;
    s_scan.step_started = millis();
    /*
     * A probe that the driver refuses (bad SPI, unsupported bitrate)
     * simply yields no frames, so the scan carries on to the next
     * candidate rather than aborting.
     */
    (void)can_iface_probe(k_probe_order[s_scan.step]);
}

static void cmd_autodetect(const char *line)
{
    if (strlen(line) != 1)   { respond_err(); return; }
    if (can_iface_is_open()) { respond_err(); return; }

    autodetect_reset();
    s_scan.active = true;
    autodetect_start_step();
    /* No reply yet: the result is sent when the scan finishes. */
}

static void autodetect_finish(void)
{
    s_scan.active = false;

    /*
     * On success, leave the adapter configured at the detected bitrate
     * but still closed, so the host only has to send 'O' or 'L' next.
     *
     * On failure the controller is sitting on whichever candidate was
     * probed last, which is by definition the wrong one - forget it, so
     * the host cannot open a mis-timed channel by accident.
     */
    if (s_scan.best_score != 0 && can_iface_configure(s_scan.best_code)) {
        Serial.write((uint8_t)'B');
        Serial.write((uint8_t)slcan_hex_chr(s_scan.best_code));
        Serial.write((uint8_t)SLCAN_CR);
    } else {
        can_iface_deconfigure();
        respond_err();
    }
}

static void pump_autodetect(void)
{
    can_frame_t frame;
    uint8_t     drained = 0;
    bool        clean;
    uint16_t    score;

    if (!s_scan.active) return;

    /* Frames seen while scanning are counted, not forwarded. */
    while (drained < CAN_RX_DRAIN_MAX && can_iface_receive(&frame)) {
        if (s_scan.frames < 255) ++s_scan.frames;
        ++drained;
    }

    if ((millis() - s_scan.step_started) < SLCAN_AUTODETECT_DWELL_MS) return;

    /*
     * Scoring: frame count decides, but a candidate that produced no
     * controller errors always beats one that did. At the wrong bit
     * timing the MCP2515 racks up receive errors and decodes little or
     * nothing; at the right one it stays clean. Listen-only mode means
     * none of this is visible to the vehicle - the controller never
     * transmits an error frame.
     */
    clean = !can_iface_has_error();
    score = (uint16_t)(s_scan.frames + (clean ? 256u : 0u));

    if (s_scan.frames >= SLCAN_AUTODETECT_MIN_FRAMES && score > s_scan.best_score) {
        s_scan.best_score = score;
        s_scan.best_code  = k_probe_order[s_scan.step];
    }

    if (++s_scan.step < AUTODETECT_STEPS) {
        autodetect_start_step();
        return;
    }
    autodetect_finish();
}

static void autodetect_cancel(void)
{
    autodetect_reset();
    can_iface_deconfigure();
}

#else /* !SLCAN_AUTODETECT */

static bool autodetect_active(void) { return false; }
static void pump_autodetect(void) {}
static void autodetect_cancel(void) {}
static void autodetect_reset(void) {}
static void cmd_autodetect(const char *line) { (void)line; respond_err(); }

#endif /* SLCAN_AUTODETECT */

/* ------------------------------------------------------------------ */

static void write_reply(char prefix, const char *s)
{
    Serial.write((uint8_t)prefix);
    Serial.write((const uint8_t *)s, strlen(s));
    Serial.write((uint8_t)SLCAN_CR);
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void handle_line(const char *line)
{
    /*
     * A scan owns the controller while it runs. 'C' aborts it - that is
     * the one escape hatch a host has if it changes its mind - and
     * everything else is refused rather than silently queued.
     */
    if (autodetect_active()) {
        if (line[0] == 'C') {
            autodetect_cancel();
            respond_ok();
        } else {
            respond_err();
        }
        return;
    }

    switch (line[0]) {
        case 'S': cmd_set_bitrate(line); break;

        /*
         * 'B' is a non-standard extension: scan the supported bitrates
         * and report which one the attached network is running at.
         */
        case 'B': cmd_autodetect(line); break;

        /*
         * 's' sets raw SJA1000 BTR0/BTR1 registers. The MCP2515 has a
         * completely different bit-timing layout, so honouring it would
         * mean putting a mis-timed node on the bus. Rejected on purpose.
         */
        case 's': respond_err(); break;

        case 'O': cmd_open(line, CAN_MODE_NORMAL);      break;
        case 'L': cmd_open(line, CAN_MODE_LISTEN_ONLY); break;
        case 'l': cmd_open(line, CAN_MODE_LOOPBACK);    break;
        case 'C': cmd_close(line);                      break;

        case 't':
        case 'T':
        case 'r':
        case 'R': cmd_transmit(line); break;

        case 'F': cmd_status(line);        break;
        case 'M': cmd_filter(line, false); break;
        case 'm': cmd_filter(line, true);  break;
        case 'Z': cmd_timestamp(line);     break;

        case 'V': write_reply('V', SLCAN_HW_VERSION SLCAN_SW_VERSION); break;
        case 'v': write_reply('v', SLCAN_SW_VERSION);                  break;
        case 'N': write_reply('N', SLCAN_SERIAL_NUMBER);               break;

        default: respond_err(); break;
    }
}

/* ------------------------------------------------------------------ */
/* Serial input - strictly non-blocking                                */
/* ------------------------------------------------------------------ */

/*
 * The original implementation busy-waited inside the read loop until a
 * CR arrived. A truncated or delayed command therefore froze the whole
 * firmware, and every CAN frame that arrived meanwhile was lost. Here we
 * only ever consume bytes that are already buffered and keep partial
 * lines across loop() iterations.
 */
static void pump_serial_input(void)
{
    /* Bound the work per pass so a fast host cannot starve the CAN side. */
    uint8_t budget = 64;

    while (budget-- && Serial.available() > 0) {
        char c = (char)Serial.read();

        /* Tolerate hosts and terminals that send CR LF or stray NULs. */
        if (c == '\n' || c == '\0') continue;

        if (c == '\r') {
            if (s_line_overflow) {
                respond_err();
            } else if (s_line_len > 0) {
                s_line[s_line_len] = '\0';
                handle_line(s_line);
            }
            /* An empty line is silently ignored, as real adapters do. */
            s_line_len      = 0;
            s_line_overflow = false;
            continue;
        }

        if (s_line_len >= (uint8_t)(sizeof(s_line) - 1)) {
            /* Latch the error and swallow the rest until the next CR. */
            s_line_overflow = true;
            continue;
        }
        s_line[s_line_len++] = c;
    }
}

/* ------------------------------------------------------------------ */
/* CAN -> host                                                         */
/* ------------------------------------------------------------------ */

static void pump_can_receive(void)
{
    char        line[SLCAN_LINE_MAX];
    can_frame_t frame;
    uint8_t     drained = 0;

    if (!can_iface_is_open()) return;
    /* During a bitrate scan the frames belong to the scanner. */
    if (autodetect_active()) return;

    /*
     * Drain up to CAN_RX_DRAIN_MAX frames per pass. The MCP2515 holds
     * two receive buffers; reading only one per loop (as the original
     * did) leaves /INT asserted and drops frames on a busy bus.
     */
    while (drained < CAN_RX_DRAIN_MAX && can_iface_receive(&frame)) {
        size_t n = slcan_encode(line, &frame, s_timestamp_on, slcan_timestamp());

#if SLCAN_NONBLOCKING_TX
        /*
         * If the host has stopped reading, writing would block here and
         * stall the drain loop. Drop the frame and tell the host about
         * it via the status register instead of hanging.
         */
        if ((size_t)Serial.availableForWrite() < n) {
            s_status_flags |= SLCAN_FLAG_DATA_OVERRUN | SLCAN_FLAG_RX_FIFO_FULL;
            ++drained;
            continue;
        }
#endif
        Serial.write((const uint8_t *)line, n);
        ++drained;
    }
}

/* ------------------------------------------------------------------ */
/* Host -> CAN                                                         */
/* ------------------------------------------------------------------ */

static void pump_can_transmit(void)
{
    if (s_tx_count == 0)           return;
    if (!can_iface_can_transmit()) { tx_queue_reset(); return; }

    switch (can_iface_send(&s_txq[s_tx_head])) {
        case CAN_TX_OK:
            tx_queue_pop();
            break;

        case CAN_TX_BUSY:
            if (++s_tx_retries >= SLCAN_TX_MAX_RETRIES) {
                s_status_flags |= SLCAN_FLAG_BUS_ERROR | SLCAN_FLAG_TX_FIFO_FULL;
                tx_queue_pop(); /* also resets the retry counter */
            }
            break;

        case CAN_TX_ERROR:
        default:
            s_status_flags |= SLCAN_FLAG_BUS_ERROR;
            tx_queue_pop();
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void slcan_init(void)
{
    s_line_len      = 0;
    s_line_overflow = false;
    s_status_flags  = 0;
    s_timestamp_on  = (SLCAN_TIMESTAMP_DEFAULT != 0);
    tx_queue_reset();
    autodetect_reset();
    can_iface_begin();
}

void slcan_poll(void)
{
    pump_serial_input();
    pump_autodetect();
    pump_can_receive();
    pump_can_transmit();
}
