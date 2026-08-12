/*
 * Host test mock of mcp_can.h (coryjfowler/MCP_CAN_lib API surface).
 *
 * The signatures below are the ones can_iface.cpp relies on. If the real
 * library ever changes them, this mock stops matching and CI's
 * arduino-cli build catches it - which is exactly the point of keeping
 * every driver call inside can_iface.cpp.
 *
 * License: MIT (see LICENSE)
 */

#ifndef MOCK_MCP_CAN_H
#define MOCK_MCP_CAN_H

#include <deque>
#include <vector>

#include "mcp_can_dfs.h"

struct MockCanFrame {
    INT32U id;
    INT8U  ext;
    INT8U  rtr;
    INT8U  len;
    INT8U  data[8];
};

/* Shared state the tests inspect and manipulate. */
struct MockCanState {
    bool                     begin_ok    = true;
    INT8U                    send_status = CAN_OK;
    INT8U                    error_state = CAN_OK;

    INT8U                    mode        = MCP_CONFIG;
    INT8U                    begin_calls = 0;
    INT8U                    last_begin_idmode = MCP_ANY;
    INT8U                    last_speed  = 0;

    std::deque<MockCanFrame> rx;   /* frames pending from the bus  */
    std::vector<MockCanFrame> tx;  /* frames the firmware sent out */

    std::vector<INT32U>      masks;
    std::vector<INT32U>      filters;

    void reset(void)
    {
        begin_ok    = true;
        send_status = CAN_OK;
        error_state = CAN_OK;
        mode        = MCP_CONFIG;
        begin_calls = 0;
        rx.clear();
        tx.clear();
        masks.clear();
        filters.clear();
    }
};

extern MockCanState g_mock_can;

class MCP_CAN {
  public:
    explicit MCP_CAN(INT8U cs) : m_cs(cs) {}

    INT8U begin(INT8U idmodeset, INT8U speedset, INT8U clockset)
    {
        (void)clockset;
        ++g_mock_can.begin_calls;
        g_mock_can.last_begin_idmode = idmodeset;
        g_mock_can.last_speed        = speedset;
        if (!g_mock_can.begin_ok) return CAN_FAILINIT;
        g_mock_can.mode = MCP_NORMAL; /* real driver leaves normal mode */
        return CAN_OK;
    }

    INT8U setMode(INT8U opMode)
    {
        g_mock_can.mode = opMode;
        return CAN_OK;
    }

    INT8U sendMsgBuf(INT32U id, INT8U ext, INT8U rtrBit, INT8U len, INT8U *buf,
                     bool wait_sent = true)
    {
        (void)wait_sent;
        if (g_mock_can.send_status != CAN_OK) return g_mock_can.send_status;

        MockCanFrame f{};
        f.id  = id;
        f.ext = ext;
        f.rtr = rtrBit;
        f.len = len;
        if (buf && len <= 8) memcpy(f.data, buf, len);
        g_mock_can.tx.push_back(f);
        return CAN_OK;
    }

    INT8U readMsgBuf(INT32U *id, INT8U *len, INT8U *buf)
    {
        if (g_mock_can.rx.empty()) return CAN_NOMSG;

        MockCanFrame f = g_mock_can.rx.front();
        g_mock_can.rx.pop_front();

        *id = f.id;
        if (f.ext) *id |= 0x80000000UL;
        if (f.rtr) *id |= 0x40000000UL;
        *len = f.len;
        memcpy(buf, f.data, 8);
        return CAN_OK;
    }

    INT8U checkReceive(void)
    {
        return g_mock_can.rx.empty() ? CAN_NOMSG : CAN_MSGAVAIL;
    }

    INT8U checkError(INT8U *err_ptr = NULL)
    {
        if (err_ptr) *err_ptr = 0;
        return g_mock_can.error_state;
    }

    INT8U init_Mask(INT8U num, INT8U ext, INT32U ulData)
    {
        (void)num;
        (void)ext;
        g_mock_can.masks.push_back(ulData);
        return CAN_OK;
    }

    INT8U init_Filt(INT8U num, INT8U ext, INT32U ulData)
    {
        (void)num;
        (void)ext;
        g_mock_can.filters.push_back(ulData);
        return CAN_OK;
    }

  private:
    INT8U m_cs;
};

#endif /* MOCK_MCP_CAN_H */
