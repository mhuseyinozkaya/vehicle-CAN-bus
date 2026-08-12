/*
 * SLCAN protocol implementation for MCP2515 - bridges vehicle CAN bus
 * to Linux SocketCAN (slcand) over serial/USB.
 * Copyright (C) 2026  Muhammed Hüseyin Özkaya
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Debug check mcp_can.h library
#ifdef DEBUG_MODE
#undef DEBUG_MODE
#endif
/* Turn off debug for the stable conversation with slcan */
#define DEBUG_MODE 0

/* Controls MCP_LOOPBACK mode */
#define LOOPBACK_MODE 1

#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <string.h>
#include <SPI.h>

/* UPDATE THIS VALUES ACCORDING TO YOUR HARDWARE */
#define SERIAL_BAUDRATE 115200
#define MCP2515_CRYSTAL_SPEED MCP_8MHZ

// MCP2515 INT pin
#define CAN0_INT 2
// Create a new CAN object instance, CS pin default 10
MCP_CAN CAN0(10);
/* UPDATE THIS VALUES ACCORDING TO YOUR HARDWARE */

// Default CAN Bus speed
static unsigned int canbus_speed = CAN_500KBPS;
// Determine CAN frame type
static bool can_tx_is_ext;

// CAN bus channel control
static bool CAN_SEND_FLAG;
static bool CANBUS_OPEN;

// Receive buffer for the handle commands from serial usb
char sl_rx_buffer[30];
// Transmit buffer for the send serial port in lawicel format
char sl_tx_buffer[30];

// Storage for received CAN frames from the CAN module
long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];

// Storage for CAN frames to be transmitted to CAN module
long unsigned int can_tx_id;
unsigned char can_tx_len = 0;
unsigned char can_tx_buf[8];

void setup(){
  Serial.begin(SERIAL_BAUDRATE);
  pinMode(CAN0_INT,INPUT_PULLUP);
  CANBUS_OPEN = false;
  CAN_SEND_FLAG = false;
}

void loop(){
  // Listen messages from serial usb port
  // If computer sends an message then process it
  if(Serial.available()){
    lawicel_parser();
  }
  // Read CAN messages from the MCP2515 controller
  if(CANBUS_OPEN == true){
    if(CAN_SEND_FLAG){
      byte status = CAN0.sendMsgBuf(can_tx_id, can_tx_is_ext, can_tx_len, can_tx_buf);
      if(status != CAN_OK)
        Serial.write('\a');
      CAN_SEND_FLAG = false;
    }
    if(!digitalRead(CAN0_INT)){
      // Read CAN frames from module
      CAN0.readMsgBuf(&rxId, &len, rxBuf);
      frame_parser();
    }
  }
}

void frame_parser(){
  // Extended CAN frames in serialcan format
  if((rxId & 0x80000000) == CAN_IS_EXTENDED){
    sl_tx_buffer[0] = 'T';
    frame_to_serialcan(true);
  }
  // Standart CAN frames in serialcan format
  else{
    sl_tx_buffer[0] = 't';
    frame_to_serialcan(false);
  }
  Serial.write(sl_tx_buffer);
  Serial.write('\r'); /* ACK to slcand */
}

void lawicel_parser(){

  read_serial_port();

  if(!strcmp(sl_rx_buffer,"O")){
#if LOOPBACK_MODE
    CAN0.setMode(MCP_LOOPBACK);
#else
    CAN0.setMode(MCP_NORMAL);
#endif
    CANBUS_OPEN = true;
    Serial.write('\r'); // ACK to slcand
  }
  else if(!strcmp(sl_rx_buffer,"C")){
    CANBUS_OPEN = false;
    Serial.write('\r'); // ACK to slcand
  }
  // Sets speed and initialize CAN bus
  else if(sl_rx_buffer[0] == 'S'){
    int speed_rate = ascii_to_hex(sl_rx_buffer[1]);
    switch(speed_rate){
      case 8:
        canbus_speed = CAN_1000KBPS; break;
      case 6:
        canbus_speed = CAN_500KBPS; break;
      case 5:
        canbus_speed = CAN_250KBPS; break;
      case 4:
        canbus_speed = CAN_125KBPS; break;
      case 3:
        canbus_speed = CAN_100KBPS; break;
      case 2:
        canbus_speed = CAN_50KBPS; break;
      case 1:
        canbus_speed = CAN_20KBPS; break;
      case 0:
        canbus_speed = CAN_10KBPS; break;
      default:
        Serial.write('\a'); // Could not set the CAN bus speed, NACK
        return;
    }
    if(CAN0.begin(MCP_ANY, canbus_speed, MCP2515_CRYSTAL_SPEED) == CAN_OK)
      Serial.write('\r'); // ACK to slcand
    else
      Serial.write('\a'); // Error happened, NACK to slcand
  }
  // Standart CAN frames received from the serial port
  else if(sl_rx_buffer[0] == 'T'){
    serialcan_to_frame(true);
    // If the message parsed successfully then switch the flag
    CAN_SEND_FLAG = true;
  }else if(sl_rx_buffer[0] == 't'){
    serialcan_to_frame(false);
    // If the message parsed successfully then switch the flag
    CAN_SEND_FLAG = true;
  }
}

void frame_to_serialcan(bool is_ext){
  uint32_t clean_id;
  uint8_t offset, high_nibble, low_nibble;
  if(is_ext){
    offset = 0x5;
    clean_id = rxId & 0x1FFFFFFF;
  }else{
    offset = 0x0;
    clean_id = rxId & 0x7FF;
  }
  // Convert ID and payload length to serialcan
  sl_tx_buffer[4+offset] = hex_to_ascii(len);
  sl_tx_buffer[5+offset+(2*len)] = '\0';
  for(int i=3+offset; 0 < i; --i){
    sl_tx_buffer[i] = hex_to_ascii(clean_id & 0x0F);
    clean_id >>= 4;
  }
  // Convert payload to serialcan format
  for(int i=0; i<len; ++i){
    high_nibble = (rxBuf[i] & 0xF0) >> 4;
    low_nibble = rxBuf[i] & 0x0F;
    sl_tx_buffer[(2*i)+5+offset] = hex_to_ascii(high_nibble);
    sl_tx_buffer[(2*i)+6+offset] = hex_to_ascii(low_nibble);
  }
}

void serialcan_to_frame(bool is_ext){
  uint32_t tmp_id = 0;
  uint8_t offset = (is_ext) ? 0x5 : 0x0;

  // Set global extended flag
  can_tx_is_ext = is_ext;

  // Convert ID and payload length to frames
  for(int i=1; i <= 3+offset; ++i){
    tmp_id <<= 4;
    tmp_id += ascii_to_hex(sl_rx_buffer[i]);
  }
  can_tx_id = tmp_id;
  can_tx_len = ascii_to_hex(sl_rx_buffer[4+offset]);
  if(can_tx_len > 8){
    Serial.write('\a');  // NACK for length overflow
    return;
  }
  for(int i=0; i<can_tx_len; ++i){
    unsigned char tmp_data = 0;
    tmp_data += ascii_to_hex(sl_rx_buffer[(2*i)+5+offset]);
    tmp_data <<= 4;
    tmp_data += ascii_to_hex(sl_rx_buffer[(2*i)+6+offset]);
    can_tx_buf[i] = tmp_data;
  }
}

void read_serial_port(){
  char recv_chr;
  int i = 0;
  while(i < sizeof(sl_rx_buffer) - 1){
    if(Serial.available()){
      recv_chr = Serial.read();
      if(recv_chr == '\r') break;
      sl_rx_buffer[i++] = recv_chr;
    }
  }
  sl_rx_buffer[i] = '\0';
}

char hex_to_ascii(uint8_t c) {
  return (c > 0x09) ? (c - 0x0A + 'A') : (c + 0x30);
}

char ascii_to_hex(char c){
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}
