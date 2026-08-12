/*
 * can_iface.h - Thin, defensive wrapper around the MCP2515 driver.
 *
 * Keeps every mcp_can call in one place so the rest of the firmware is
 * independent of which MCP2515 library happens to be installed, and so
 * the controller state machine (uninitialised -> configured -> open) is
 * enforced in exactly one spot.
 *
 * License: MIT (see LICENSE)
 */

#ifndef CAN_IFACE_H
#define CAN_IFACE_H

#include <stdbool.h>
#include <stdint.h>

#include "slcan_codec.h"

/* Channel operating mode requested by the host. */
typedef enum {
    CAN_MODE_LISTEN_ONLY = 0, /* 'L' - never drives the bus, not even ACK */
    CAN_MODE_NORMAL      = 1, /* 'O' - full participant, sends ACKs       */
    CAN_MODE_LOOPBACK    = 2  /* 'l' - self test, fully isolated from bus */
} can_mode_t;

/* Result of a transmit attempt. */
typedef enum {
    CAN_TX_OK = 0,
    CAN_TX_BUSY,  /* controller buffers full - retry later */
    CAN_TX_ERROR  /* hard failure                          */
} can_tx_result_t;

/* One-time SPI / pin setup. Does not touch the bus. */
void can_iface_begin(void);

/*
 * Configures the controller for `bitrate_code` (an SLCAN 'S' digit, 0-8)
 * and leaves it in configuration mode - i.e. still electrically silent.
 * Returns false for unsupported bitrates or if the MCP2515 does not
 * respond over SPI.
 */
bool can_iface_configure(uint8_t bitrate_code);

/* True once can_iface_configure() has succeeded at least once. */
bool can_iface_is_configured(void);

/*
 * Installs the SLCAN acceptance code/mask. SLCAN inherits SJA1000
 * semantics where a mask bit of 1 means "don't care"; the MCP2515 uses
 * the opposite convention, and this function performs the inversion.
 * Only legal while the channel is closed. Takes effect on the next open.
 */
void can_iface_set_filter(uint32_t code, uint32_t mask);

/* Clears any acceptance filter: every frame is received. */
void can_iface_clear_filter(void);

/* Brings the channel up in `mode`. Returns false if not configured. */
bool can_iface_open(can_mode_t mode);

/*
 * Configures `bitrate_code` and opens the channel listen-only in a
 * single step, deliberately ignoring any acceptance filter. Used by the
 * bitrate scanner: probing must see every frame on the wire, and it must
 * never drive the bus while guessing at the wrong bit timing.
 */
bool can_iface_probe(uint8_t bitrate_code);

/*
 * Closes the channel and forgets the configured bitrate, so the host has
 * to send 'S' again before it can open anything. Used after an aborted
 * or unsuccessful scan: the controller is then sitting on whichever
 * bitrate happened to be probed last, and letting the host open that
 * would put a mis-timed node on the bus.
 */
void can_iface_deconfigure(void);

/*
 * Takes the channel down and parks the controller in configuration mode
 * so it stops acknowledging traffic on the vehicle bus. Leaving the
 * MCP2515 in normal mode after a close would keep it ACKing frames -
 * an invisible way to disturb the very bus you are only observing.
 */
void can_iface_close(void);

bool can_iface_is_open(void);

/* True when the channel is open in a mode that permits transmission. */
bool can_iface_can_transmit(void);

/* Non-blocking-ish send of one frame. */
can_tx_result_t can_iface_send(const can_frame_t *f);

/*
 * Pops one frame from the controller. Returns false when nothing is
 * pending. Extended/remote flags are decoded from the driver's ID bits.
 */
bool can_iface_receive(can_frame_t *f);

/* Raw MCP2515 EFLG-derived error indication, for the 'F' command. */
bool can_iface_has_error(void);

#endif /* CAN_IFACE_H */
