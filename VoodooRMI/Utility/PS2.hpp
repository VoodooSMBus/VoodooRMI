/* SPDX-License-Identifier: GPL-2.0-only
 * PS2 constants
 * https://github.com/torvalds/linux/tree/master/include/linux/libps2.h
 */

#ifndef PS2_h
#define PS2_h

#define PSMOUSE_OOB_EXTRA_BTNS        0x01

#define PS2_CMD_SETSCALE11    0x00e6
#define PS2_CMD_SETRES        0x10e8
#define PS2_CMD_GETID        0x02f2
#define PS2_CMD_RESET_BAT    0x02ff

#define PS2_RET_BAT        0xaa
#define PS2_RET_ID        0x00
#define PS2_RET_ACK        0xfa
#define PS2_RET_NAK        0xfe
#define PS2_RET_ERR        0xfc

#define PS2_FLAG_ACK        BIT(0)    /* Waiting for ACK/NAK */
#define PS2_FLAG_CMD        BIT(1)    /* Waiting for a command to finish */
#define PS2_FLAG_CMD1        BIT(2)    /* Waiting for the first byte of command response */
#define PS2_FLAG_WAITID        BIT(3)    /* Command executing is GET ID */
#define PS2_FLAG_NAK        BIT(4)    /* Last transmission was NAKed */
#define PS2_FLAG_ACK_CMD    BIT(5)    /* Waiting to ACK the command (first) byte */

#endif /* PS2_h */
