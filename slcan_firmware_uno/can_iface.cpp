/*
 * can_iface.cpp - MCP2515 wrapper. See can_iface.h.
 *
 * License: MIT (see LICENSE)
 */

/*
 * The mcp_can library ships a DEBUG_MODE switch that prints to Serial.
 * Any stray text on the serial link corrupts the SLCAN stream, so it is
 * forced off here - before the header is pulled in.
 */
#ifdef DEBUG_MODE
#undef DEBUG_MODE
#endif
#define DEBUG_MODE 0

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include <mcp_can.h>
#include <mcp_can_dfs.h>

#include "can_iface.h"
#include "config.h"

/* Driver ID flags (identical in the coryjfowler and Seeed forks). */
#define MCP_ID_EXT_FLAG 0x80000000UL
#define MCP_ID_RTR_FLAG 0x40000000UL

static MCP_CAN s_can(MCP2515_CS_PIN);

static bool     s_configured = false;
static bool     s_open       = false;
static uint8_t  s_speed      = CAN_500KBPS;
static can_mode_t s_mode     = CAN_MODE_LISTEN_ONLY;

static bool     s_filter_active = false;
static uint32_t s_filter_code   = 0;
static uint32_t s_filter_mask   = 0xFFFFFFFFUL; /* SLCAN: all "don't care" */

/* Which identifier-acceptance mode the last begin() used, so open() can
 * skip a redundant re-initialisation (see the comment in can_iface_open). */
static bool s_begun_with_filters = false;

/* ------------------------------------------------------------------ */

void can_iface_begin(void)
{
    pinMode(MCP2515_INT_PIN, INPUT_PULLUP);
    s_configured    = false;
    s_open          = false;
    s_filter_active = false;
}

/*
 * Maps an SLCAN 'S' digit to an mcp_can speed constant.
 * S7 (800 kbit/s) is intentionally unsupported: the MCP2515 bit-timing
 * tables in the driver have no entry for it, and silently substituting a
 * different bitrate would put a mis-timed node on a live vehicle bus.
 */
static bool speed_from_code(uint8_t code, uint8_t *out)
{
    switch (code) {
        case 0: *out = CAN_10KBPS;   return true;
        case 1: *out = CAN_20KBPS;   return true;
        case 2: *out = CAN_50KBPS;   return true;
        case 3: *out = CAN_100KBPS;  return true;
        case 4: *out = CAN_125KBPS;  return true;
        case 5: *out = CAN_250KBPS;  return true;
        case 6: *out = CAN_500KBPS;  return true;
        case 8: *out = CAN_1000KBPS; return true;
        default: return false;
    }
}

bool can_iface_configure(uint8_t bitrate_code)
{
    uint8_t speed;

    if (!speed_from_code(bitrate_code, &speed)) return false;

    /*
     * Probe the controller now rather than at open time, so a wiring or
     * crystal problem is reported while the host tool is still expecting
     * an answer.
     *
     * The driver's begin() ends by switching to normal mode, so we force
     * configuration mode back immediately. The brief window in between
     * is harmless: after leaving configuration mode the MCP2515 must see
     * 11 consecutive recessive bits before it joins the bus, and we are
     * back in configuration mode long before that happens.
     */
    if (s_can.begin(MCP_ANY, speed, MCP2515_CRYSTAL) != CAN_OK) {
        s_configured = false;
        return false;
    }
    s_can.setMode(MCP_CONFIG);

    s_speed              = speed;
    s_configured         = true;
    s_open               = false;
    s_begun_with_filters = false;
    return true;
}

bool can_iface_is_configured(void) { return s_configured; }

void can_iface_set_filter(uint32_t code, uint32_t mask)
{
    s_filter_code   = code;
    s_filter_mask   = mask;
    /* An all-ones SLCAN mask means "accept everything" - not a filter. */
    s_filter_active = (mask != 0xFFFFFFFFUL);
}

void can_iface_clear_filter(void)
{
    s_filter_code   = 0;
    s_filter_mask   = 0xFFFFFFFFUL;
    s_filter_active = false;
}

static void apply_filters(void)
{
    /*
     * SJA1000/SLCAN: mask bit 1 == don't care.
     * MCP2515:       mask bit 1 == must match the filter.
     * Hence the inversion. Without it, an SLCAN mask would filter out
     * precisely the frames the user asked to keep.
     */
    const bool     ext      = (s_filter_code & ~CAN_SFF_MASK) != 0;
    const uint32_t id_mask  = ext ? CAN_EFF_MASK : CAN_SFF_MASK;
    const uint32_t mcp_mask = (~s_filter_mask) & id_mask;
    const uint32_t mcp_code = s_filter_code & id_mask;

    /* Both receive buffers, all six filters, same acceptance criteria. */
    s_can.init_Mask(0, ext ? 1 : 0, mcp_mask);
    s_can.init_Mask(1, ext ? 1 : 0, mcp_mask);
    for (uint8_t i = 0; i < 6; ++i) {
        s_can.init_Filt(i, ext ? 1 : 0, mcp_code);
    }
}

bool can_iface_open(can_mode_t mode)
{
    if (!s_configured) return false;

#if SLCAN_READ_ONLY
    /* Hard safety interlock: never leave listen-only in read-only builds. */
    if (mode == CAN_MODE_NORMAL) return false;
#endif

    /*
     * begin() is only re-run when the identifier-acceptance mode has to
     * change: MCP_ANY makes the receive buffers ignore the filters, so
     * filtering requires MCP_STDEXT. Skipping the redundant call matters
     * because begin() ends in normal mode - the one mode that drives the
     * bus - and we would rather not pass through it at all when opening
     * a listen-only channel.
     */
    if (s_filter_active != s_begun_with_filters) {
        const uint8_t idmode = s_filter_active ? MCP_STDEXT : MCP_ANY;
        if (s_can.begin(idmode, s_speed, MCP2515_CRYSTAL) != CAN_OK) return false;
        s_begun_with_filters = s_filter_active;
    }

    if (s_filter_active) {
        /* init_Mask/init_Filt are only valid in configuration mode. */
        s_can.setMode(MCP_CONFIG);
        apply_filters();
    }

    switch (mode) {
        case CAN_MODE_NORMAL:   s_can.setMode(MCP_NORMAL);     break;
        case CAN_MODE_LOOPBACK: s_can.setMode(MCP_LOOPBACK);   break;
        case CAN_MODE_LISTEN_ONLY:
        default:                s_can.setMode(MCP_LISTENONLY); break;
    }

    s_mode = mode;
    s_open = true;
    return true;
}

bool can_iface_probe(uint8_t bitrate_code)
{
    uint8_t speed;

    if (!speed_from_code(bitrate_code, &speed)) return false;

    /*
     * begin() also resets the controller, which clears the error flags
     * left over from the previous candidate bitrate. That reset is what
     * makes each probe step independent - the error counters are the
     * primary signal that a bitrate is wrong.
     */
    if (s_can.begin(MCP_ANY, speed, MCP2515_CRYSTAL) != CAN_OK) return false;
    s_can.setMode(MCP_LISTENONLY);

    s_speed              = speed;
    s_configured         = true;
    s_begun_with_filters = false;
    s_mode               = CAN_MODE_LISTEN_ONLY;
    s_open               = true;
    return true;
}

void can_iface_close(void)
{
    if (s_configured) s_can.setMode(MCP_CONFIG);
    s_open = false;
}

void can_iface_deconfigure(void)
{
    can_iface_close();
    s_configured = false;
}

bool can_iface_is_open(void) { return s_open; }

bool can_iface_can_transmit(void)
{
    if (!s_open) return false;

    /*
     * Loopback is allowed even in a read-only build: in that mode the
     * MCP2515 routes the frame straight into its own receive buffer and
     * holds TXCAN recessive, so nothing reaches the vehicle bus. Keeping
     * it available is what makes a read-only adapter self-testable
     * without reflashing it.
     */
    if (s_mode == CAN_MODE_LOOPBACK) return true;

#if SLCAN_READ_ONLY
    return false;
#else
    return s_mode == CAN_MODE_NORMAL;
#endif
}

can_tx_result_t can_iface_send(const can_frame_t *f)
{
    uint8_t status;

    if (!can_iface_can_transmit()) return CAN_TX_ERROR;

    /*
     * wait_sent = false. The driver's blocking mode spins for up to 50 ms
     * waiting for the frame to leave the controller, which on a busy bus
     * stalls the RX drain and loses frames. We only need to know whether
     * the frame made it into a hardware TX buffer; if none was free the
     * caller re-queues it.
     */
    status = s_can.sendMsgBuf((INT32U)f->id,
                              (INT8U)(f->ext ? 1 : 0),
                              (INT8U)(f->rtr ? 1 : 0),
                              (INT8U)f->dlc,
                              (INT8U *)f->data,
                              false);

    if (status == CAN_OK)          return CAN_TX_OK;
    if (status == CAN_GETTXBFTIMEOUT) return CAN_TX_BUSY;
    return CAN_TX_ERROR;
}

bool can_iface_receive(can_frame_t *f)
{
    INT32U raw_id = 0;
    INT8U  len    = 0;
    INT8U  buf[CAN_MAX_DLC];

    if (!s_open) return false;
    if (s_can.checkReceive() != CAN_MSGAVAIL) return false;
    if (s_can.readMsgBuf(&raw_id, &len, buf) != CAN_OK) return false;

    f->ext = (raw_id & MCP_ID_EXT_FLAG) != 0;
    f->rtr = (raw_id & MCP_ID_RTR_FLAG) != 0;
    f->id  = (uint32_t)raw_id & (f->ext ? CAN_EFF_MASK : CAN_SFF_MASK);

    /* Defensive: a glitched SPI read must not overflow the payload. */
    if (len > CAN_MAX_DLC) len = CAN_MAX_DLC;
    f->dlc = (uint8_t)len;

    memset(f->data, 0, sizeof(f->data));
    if (!f->rtr) memcpy(f->data, buf, len);

    return true;
}

bool can_iface_has_error(void)
{
    return s_configured && (s_can.checkError() != CAN_OK);
}
