/*
 * Host test mock of mcp_can_dfs.h.
 *
 * Only the constants the firmware actually references are defined, with
 * the same values as the upstream coryjfowler/MCP_CAN_lib header, so a
 * mismatch in the real build would show up here too.
 *
 * License: MIT (see LICENSE)
 */

#ifndef MOCK_MCP_CAN_DFS_H
#define MOCK_MCP_CAN_DFS_H

#include <stdint.h>

typedef uint8_t  INT8U;
typedef uint32_t INT32U;

/* Return codes */
#define CAN_OK             (0)
#define CAN_FAILINIT       (1)
#define CAN_FAILTX         (2)
#define CAN_MSGAVAIL       (3)
#define CAN_NOMSG          (4)
#define CAN_CTRLERROR      (5)
#define CAN_GETTXBFTIMEOUT (6)
#define CAN_SENDMSGTIMEOUT (7)
#define CAN_FAIL           (0xff)

/* Operating modes */
#define MCP_NORMAL     0x00
#define MCP_SLEEP      0x20
#define MCP_LOOPBACK   0x40
#define MCP_LISTENONLY 0x60
#define MCP_CONFIG     0x80

/* Identifier acceptance modes for begin() */
#define MCP_STDEXT 0
#define MCP_STD    1
#define MCP_EXT    2
#define MCP_ANY    3

/* Crystal options */
#define MCP_20MHZ 0
#define MCP_16MHZ 1
#define MCP_8MHZ  2

/* Bitrate constants (values are opaque; only identity matters) */
#define CAN_5KBPS    1
#define CAN_10KBPS   2
#define CAN_20KBPS   3
#define CAN_31K25BPS 4
#define CAN_33KBPS   5
#define CAN_40KBPS   6
#define CAN_50KBPS   7
#define CAN_80KBPS   8
#define CAN_83K3BPS  9
#define CAN_95KBPS   10
#define CAN_100KBPS  11
#define CAN_125KBPS  12
#define CAN_200KBPS  13
#define CAN_250KBPS  14
#define CAN_500KBPS  15
#define CAN_666KBPS  16
#define CAN_1000KBPS 17

#endif /* MOCK_MCP_CAN_DFS_H */
