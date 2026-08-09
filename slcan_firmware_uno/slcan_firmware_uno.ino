/* SLCAN protocol implementation - bridges vehicle CAN bus to */
/* Linux SocketCAN (slcand) over serial/USB */

/* @Author: Muhammed Hüseyin Özkaya */

// Debug check mcp_can.h library
#ifdef DEBUG_MODE
#undef DEBUG_MODE
#endif
/* Turn off debug for the stable conversation with slcan */
#define DEBUG_MODE 0

/* Controls MCP_LOOPBACK mode */
#define LOOPBACK_MODE 0

#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <string.h>
#include <stdio.h>
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

// CAN bus channel control
static bool is_canbus_open;
static bool cansend_flag;

// Receive buffer for the handle commands from serial usb
char sl_rx_buffer[32];
// Transmit buffer for the send serial port in lawicel format
char sl_tx_buffer[32];

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
  is_canbus_open = false;
  cansend_flag = false;
}

void loop(){
  // Listen messages from serial usb port
  /* Waiting in idle for handle slcan commands */
  if(Serial.available()){
    lawicel_parser();
  }

  while(is_canbus_open == true){
    // Read CAN messages from the MCP2515 controller
    if(!digitalRead(CAN0_INT)){
      CAN0.readMsgBuf(&rxId, &len, rxBuf);

      // Standart CAN frames in lawicel format
      sprintf(sl_tx_buffer, "t%.3lX%1d", rxId, len);
      Serial.write(sl_tx_buffer);

      // Write CAN data buffer to serial port
      for(byte i = 0; i<len; i++){
        sprintf(sl_tx_buffer, "%.2X", rxBuf[i]);
        Serial.write(sl_tx_buffer);
      }
      Serial.write('\r'); /* ACK to slcand */
    }
    // If computer sends an message then process it
    if(Serial.available()){
      lawicel_parser();
    }
    if(cansend_flag){
      byte status = CAN0.sendMsgBuf(can_tx_id, 0, can_tx_len, can_tx_buf);
      /* Harici bir LED takarak hata kodlarını donanım üzerinde göstermeye çalış slcan protokolünü bozmaması için atlandı */
      cansend_flag = false;
    }
  }
}

void lawicel_parser(){
  /* Gelen Komutlar String nesnesi olarak alınıyor ve char dizisine dönüştürülüyor, sonradan optimizasyon sağlanabilir */
  String rx_string = Serial.readStringUntil('\r');
  rx_string.toCharArray(sl_rx_buffer, sizeof(sl_rx_buffer));

  if(!strcmp(sl_rx_buffer,"O")){
#if LOOPBACK_MODE
    CAN0.setMode(MCP_LOOPBACK);
#else
    CAN0.setMode(MCP_NORMAL);
#endif
    is_canbus_open = true;
    Serial.write('\r'); // ACK to slcand
  }
  else if(!strcmp(sl_rx_buffer,"C")){
    is_canbus_open = false;
    Serial.write('\r'); // ACK to slcand
  }
  // Sets speed and initialize CAN bus
  else if(sl_rx_buffer[0] == 'S'){
    int speed_rate = sl_rx_buffer[1] - '0';
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
  else if(sl_rx_buffer[0] == 't'){
    // Save CAN ID and data length from received message
    int tmp_len;
    sscanf(sl_rx_buffer, "t%3lX%1d", &can_tx_id, &tmp_len);
    can_tx_len = (unsigned char)tmp_len;

    // And then read actual data and store in buffer
    char* ptr = &sl_rx_buffer[5];
    for(int i=0; i<can_tx_len; ++i){
      sscanf(ptr,"%2hhx",&can_tx_buf[i]);
      ptr += 2;
    }
    // If the message parsed successfully then switch the flag
    cansend_flag = true;
  }
}