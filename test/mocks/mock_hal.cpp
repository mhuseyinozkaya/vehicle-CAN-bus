/* Host test mock - definitions of the globals declared in the headers. */

#include "Arduino.h"
#include "mcp_can.h"

MockSerial   Serial;
MockCanState g_mock_can;

static uint32_t s_millis;

void pinMode(uint8_t, uint8_t) {}
int  digitalRead(uint8_t) { return HIGH; }

uint32_t millis(void) { return s_millis; }
void     mock_set_millis(uint32_t ms) { s_millis = ms; }
