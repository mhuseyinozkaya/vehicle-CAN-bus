/*
 * slcan_protocol.h - SLCAN command interpreter and frame pump.
 *
 * License: MIT (see LICENSE)
 */

#ifndef SLCAN_PROTOCOL_H
#define SLCAN_PROTOCOL_H

#include <stdint.h>

/*
 * SLCAN status bits, as reported by the 'F' command. The layout follows
 * the original LAWICEL CANUSB so existing host tools can interpret it.
 */
#define SLCAN_FLAG_RX_FIFO_FULL  0x01
#define SLCAN_FLAG_TX_FIFO_FULL  0x02
#define SLCAN_FLAG_ERROR_WARNING 0x04
#define SLCAN_FLAG_DATA_OVERRUN  0x08
#define SLCAN_FLAG_ERROR_PASSIVE 0x20
#define SLCAN_FLAG_ARB_LOST      0x40
#define SLCAN_FLAG_BUS_ERROR     0x80

/* Call once from setup(), after Serial.begin(). */
void slcan_init(void);

/*
 * Call from loop(). Never blocks: it consumes whatever serial input is
 * available, drains a bounded number of CAN frames towards the host and
 * services the transmit queue, then returns.
 */
void slcan_poll(void);

#endif /* SLCAN_PROTOCOL_H */
