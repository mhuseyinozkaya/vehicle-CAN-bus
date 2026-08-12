/*
 * config.h - All user-tunable settings live here.
 *
 * Nothing outside this file should need to be edited when adapting the
 * firmware to different hardware.
 *
 * Project: vehicle-CAN-bus / Arduino MCP2515 SLCAN adapter
 * Original author: Muhammed Huseyin Ozkaya
 * License: MIT (see LICENSE)
 */

#ifndef SLCAN_CONFIG_H
#define SLCAN_CONFIG_H

/* ------------------------------------------------------------------ */
/* Hardware wiring                                                     */
/* ------------------------------------------------------------------ */

/* MCP2515 chip-select pin (SPI SS). */
#define MCP2515_CS_PIN 10

/* MCP2515 /INT pin. Must be a pin that supports digitalRead(); an
 * external interrupt pin (2 or 3 on the Uno) is recommended even though
 * this firmware polls, so the wiring stays compatible with future work. */
#define MCP2515_INT_PIN 2

/* Crystal soldered on the MCP2515 breakout board.
 * Blue "niren" style boards are usually MCP_8MHZ, others MCP_16MHZ.
 * If the bus never syncs, this is the first thing to double-check.
 * Valid: MCP_8MHZ, MCP_16MHZ, MCP_20MHZ                                */
#define MCP2515_CRYSTAL MCP_8MHZ

/* ------------------------------------------------------------------ */
/* Serial link to the host                                             */
/* ------------------------------------------------------------------ */

/*
 * SLCAN is an ASCII protocol: one 8-byte standard frame costs 26 bytes
 * on the wire (plus 1 stop/start overhead per byte on the UART).
 *
 *   115200 baud ->  ~443 frames/s max
 *   500000 baud -> ~1923 frames/s max
 *
 * A busy 500 kbit/s vehicle bus easily exceeds 2000 frames/s, so the
 * UART - not the CAN controller - is the bottleneck. Raise this to
 * 500000 if your USB-serial adapter is stable at that rate, and pass the
 * same value to scripts/slcan-up.sh.
 *
 * 500000 is an exact divisor of the 16 MHz Uno clock (0.0% error), so it
 * is actually *more* reliable than 115200 (-3.5% error, corrected by U2X).
 */
#define SERIAL_BAUDRATE 115200UL

/* ------------------------------------------------------------------ */
/* Behaviour                                                           */
/* ------------------------------------------------------------------ */

/*
 * SAFETY SWITCH.
 *
 * When set to 1 the adapter cannot write to the CAN bus: the 'O' (open
 * normal) command is rejected, only 'L' (listen-only) works, and all
 * transmit commands are answered with an error.
 *
 * The 'l' (loopback) mode stays available so the adapter can still be
 * self-tested: the MCP2515 holds TXCAN recessive in loopback, so nothing
 * reaches the vehicle bus.
 *
 * Recommended while sniffing a real vehicle. Set to 0 only when you
 * intentionally need to inject frames.
 *
 * Can also be forced from the build system, e.g. PlatformIO:
 *   build_flags = -DSLCAN_READ_ONLY=1
 */
#ifndef SLCAN_READ_ONLY
#define SLCAN_READ_ONLY 0
#endif

/*
 * Number of frames buffered on the way OUT to the CAN bus. Each slot
 * costs 16 bytes of SRAM. 8 slots is a good balance on an Uno (2 KB).
 */
#define SLCAN_TX_QUEUE_LEN 8

/*
 * Upper bound on how many CAN frames are drained from the MCP2515 in a
 * single loop() pass. Prevents a flooded bus from starving the serial
 * command parser. Keep >= 2 (the MCP2515 has two RX buffers).
 */
#define CAN_RX_DRAIN_MAX 4

/*
 * If the host stops reading, the USB serial TX buffer fills up and
 * Serial.write() blocks - which stalls the CAN drain loop and causes
 * silent frame loss. With this enabled we drop the frame instead and
 * raise the RX-overrun status bit, which the host can read with 'F'.
 */
#define SLCAN_NONBLOCKING_TX 1

/*
 * Timestamp default. The host can still toggle it at runtime with
 * 'Z0'/'Z1'. slcand does not use timestamps, so the default is off.
 */
#define SLCAN_TIMESTAMP_DEFAULT 0

/*
 * Bitrate scanner ('B' command). Listens on each supported bitrate in
 * turn - always in listen-only mode, so a wrong guess cannot disturb the
 * bus - and reports the one that produced clean traffic.
 *
 * Set to 0 to save roughly 400 bytes of flash if you always know the
 * bitrate of the network you are attaching to.
 */
#ifndef SLCAN_AUTODETECT
#define SLCAN_AUTODETECT 1
#endif

/*
 * How long to listen on each candidate bitrate, in milliseconds.
 * Vehicle buses carry periodic frames every 10-100 ms, so 200 ms is
 * comfortably enough while keeping a full scan under two seconds.
 */
#define SLCAN_AUTODETECT_DWELL_MS 200

/*
 * How many frames a bitrate must yield before it is considered a match.
 * Raising this rejects the occasional frame a wrong bitrate decodes by
 * accident, at the cost of missing very quiet networks.
 */
#define SLCAN_AUTODETECT_MIN_FRAMES 2

/* ------------------------------------------------------------------ */
/* Identification strings reported to the host                         */
/* ------------------------------------------------------------------ */

/* 'V' -> hardware + software version, 4 hex digits total. */
#define SLCAN_HW_VERSION "10"
#define SLCAN_SW_VERSION "20"
/* 'N' -> serial number, 4 characters. */
#define SLCAN_SERIAL_NUMBER "0001"

/* ------------------------------------------------------------------ */
/* Compile-time sanity checks                                          */
/* ------------------------------------------------------------------ */

#if SLCAN_TX_QUEUE_LEN < 2
#error "SLCAN_TX_QUEUE_LEN must be at least 2"
#endif

#if CAN_RX_DRAIN_MAX < 2
#error "CAN_RX_DRAIN_MAX must be at least 2 (the MCP2515 has two RX buffers)"
#endif

#endif /* SLCAN_CONFIG_H */
