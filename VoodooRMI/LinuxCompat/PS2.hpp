/* SPDX-License-Identifier: GPL-2.0-only
 * PS2 constants
 * https://github.com/torvalds/linux/tree/master/include/linux/libps2.h
 * https://github.com/torvalds/linux/blob/master/drivers/input/mouse/psmouse-base.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/mouse/trackpoint.c
 */

#ifndef PS2_h
#define PS2_h

#define PSMOUSE_OOB_EXTRA_BTNS        0x01

#define PS2_CMD_SETSCALE11  0x00e6
#define PS2_CMD_SETSCALE21  0x00e7
#define PS2_CMD_SETRES      0x10e8
#define PS2_CMD_SETRATE     0x10f3
#define PS2_CMD_GETID       0x02f2
#define PS2_CMD_RESET_BAT   0x02ff

#define PS2_RET_BAT         0xaa
#define PS2_RET_ID          0x00
#define PS2_RET_ACK         0xfa
#define PS2_RET_NAK         0xfe
#define PS2_RET_ERR         0xfc

#define PS2_FLAG_ACK        BIT(0)    /* Waiting for ACK/NAK */
#define PS2_FLAG_CMD        BIT(1)    /* Waiting for a command to finish */
#define PS2_FLAG_CMD1       BIT(2)    /* Waiting for the first byte of command response */
#define PS2_FLAG_WAITID     BIT(3)    /* Command executing is GET ID */
#define PS2_FLAG_NAK        BIT(4)    /* Last transmission was NAKed */
#define PS2_FLAG_ACK_CMD    BIT(5)    /* Waiting to ACK the command (first) byte */

#define PSMOUSE_CMD_ENABLE 0x00f4

#define MAKE_PS2_CMD(params, results, cmd) ((params<<12) | (results<<8) | (cmd))

/*
 * These constants are from the TrackPoint System
 * Engineering documentation Version 4 from IBM Watson
 * research:
 *    http://wwwcssrv.almaden.ibm.com/trackpoint/download.html
 */

#define TP_COMMAND        0xE2    /* Commands start with this */

#define TP_READ_ID        0xE1    /* Sent for device identification */

/*
 * Valid first byte responses to the "Read Secondary ID" (0xE1) command.
 * 0x01 was the original IBM trackpoint, others implement very limited
 * subset of trackpoint features.
 */
#define TP_VARIANT_IBM        0x01
#define TP_VARIANT_ALPS       0x02
#define TP_VARIANT_ELAN       0x03
#define TP_VARIANT_NXP        0x04

/*
 * Commands
 */
#define TP_RECALIB        0x51    /* Recalibrate */
#define TP_POWER_DOWN        0x44    /* Can only be undone through HW reset */
#define TP_EXT_DEV        0x21    /* Determines if external device is connected (RO) */
#define TP_EXT_BTN        0x4B    /* Read extended button status */
#define TP_POR            0x7F    /* Execute Power on Reset */
#define TP_POR_RESULTS        0x25    /* Read Power on Self test results */
#define TP_DISABLE_EXT        0x40    /* Disable external pointing device */
#define TP_ENABLE_EXT        0x41    /* Enable external pointing device */

/*
 * Mode manipulation
 */
#define TP_SET_SOFT_TRANS    0x4E    /* Set mode */
#define TP_CANCEL_SOFT_TRANS    0xB9    /* Cancel mode */
#define TP_SET_HARD_TRANS    0x45    /* Mode can only be set */

/*
 * Commands
 */
#define TP_RECALIB        0x51    /* Recalibrate */
#define TP_POWER_DOWN     0x44    /* Can only be undone through HW reset */
#define TP_EXT_DEV        0x21    /* Determines if external device is connected (RO) */
#define TP_EXT_BTN        0x4B    /* Read extended button status */
#define TP_POR            0x7F    /* Execute Power on Reset */
#define TP_POR_RESULTS    0x25    /* Read Power on Self test results */
#define TP_DISABLE_EXT    0x40    /* Disable external pointing device */
#define TP_ENABLE_EXT     0x41    /* Enable external pointing device */

/*
 * Mode manipulation
 */
#define TP_SET_SOFT_TRANS       0x4E    /* Set mode */
#define TP_CANCEL_SOFT_TRANS    0xB9    /* Cancel mode */
#define TP_SET_HARD_TRANS       0x45    /* Mode can only be set */

/*
 * Register oriented commands/properties
 */
#define TP_WRITE_MEM        0x81

/* Power on Self Test Results */
#define TP_POR_SUCCESS        0x3B

static const char * const trackpoint_variants[] = {
    [TP_VARIANT_IBM]    = "IBM",
    [TP_VARIANT_ALPS]   = "ALPS",
    [TP_VARIANT_ELAN]   = "Elan",
    [TP_VARIANT_NXP]    = "NXP",
};

#endif /* PS2_h */
