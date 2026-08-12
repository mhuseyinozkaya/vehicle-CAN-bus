/*
 * slcan_firmware_uno - SLCAN (LAWICEL) adapter for Arduino + MCP2515.
 *
 * Bridges a vehicle CAN bus to Linux SocketCAN via slcand over the USB
 * serial link.
 *
 * All tunable settings live in config.h - this file only wires the
 * modules together.
 *
 *   config.h          hardware and behaviour settings
 *   slcan_codec.*     ASCII <-> CAN frame codec (host-testable, no HAL)
 *   can_iface.*       MCP2515 wrapper and channel state machine
 *   slcan_protocol.*  command interpreter and frame pump
 *
 * Original author: Muhammed Huseyin Ozkaya
 * License: MIT (see LICENSE)
 */

#include "config.h"
#include "slcan_protocol.h"

void setup(void)
{
    Serial.begin(SERIAL_BAUDRATE);

    /*
     * Deliberately no banner, no debug print, no LED blink pattern on
     * the serial port: slcand starts parsing immediately and any extra
     * byte on the link is interpreted as a malformed SLCAN frame.
     */
    slcan_init();
}

void loop(void)
{
    slcan_poll();
}
