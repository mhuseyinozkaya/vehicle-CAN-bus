/*
 * Host test mock of Arduino.h.
 *
 * Provides just enough of the core API for can_iface.cpp and
 * slcan_protocol.cpp to compile and run on a development machine, plus
 * hooks the tests use to drive the fake serial port.
 *
 * License: MIT (see LICENSE)
 */

#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2
#define HIGH         1
#define LOW          0

void     pinMode(uint8_t pin, uint8_t mode);
int      digitalRead(uint8_t pin);
uint32_t millis(void);

/* Test hook: advance the fake clock. */
void mock_set_millis(uint32_t ms);

class MockSerial {
  public:
    void begin(unsigned long) {}

    int available(void)
    {
        return (int)(rx.size() - rx_pos);
    }

    int read(void)
    {
        if (rx_pos >= rx.size()) return -1;
        return (unsigned char)rx[rx_pos++];
    }

    /* Fake UART transmit buffer size, matching the Uno's 64 bytes. */
    int availableForWrite(void) { return write_space; }

    size_t write(uint8_t c)
    {
        tx.push_back((char)c);
        return 1;
    }

    size_t write(const uint8_t *buf, size_t n)
    {
        tx.append((const char *)buf, n);
        return n;
    }

    /* ---- test hooks ---- */
    void mock_feed(const std::string &s)
    {
        rx += s;
    }

    void mock_reset(void)
    {
        rx.clear();
        tx.clear();
        rx_pos      = 0;
        write_space = 64;
    }

    std::string mock_take_tx(void)
    {
        std::string out = tx;
        tx.clear();
        return out;
    }

    int write_space = 64;

  private:
    std::string rx;
    std::string tx;
    size_t      rx_pos = 0;
};

extern MockSerial Serial;

#endif /* MOCK_ARDUINO_H */
