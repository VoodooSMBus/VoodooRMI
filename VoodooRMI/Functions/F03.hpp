/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef F03_hpp
#define F03_hpp

#include "../RMIBus.hpp"

#define RMI_F03_RX_DATA_OFB        0x01
#define RMI_F03_OB_SIZE            2

#define RMI_F03_OB_OFFSET        2
#define RMI_F03_OB_DATA_OFFSET        1
#define RMI_F03_OB_FLAG_TIMEOUT        BIT(6)
#define RMI_F03_OB_FLAG_PARITY        BIT(7)

#define RMI_F03_DEVICE_COUNT        0x07
#define RMI_F03_BYTES_PER_DEVICE    0x07
#define RMI_F03_BYTES_PER_DEVICE_SHIFT    4
#define RMI_F03_QUEUE_LENGTH        0x0F

#define PSMOUSE_OOB_EXTRA_BTNS        0x01

// VoodooPS2
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// PS/2 Command Primitives
//
// o  kPS2C_ReadDataPort:
//    o  Description: Reads the next available byte off the data port (60h).
//    o  Out Field:   Holds byte that was read.
//
// o  kPS2C_ReadDataAndCompare:
//    o  Description: Reads the next available byte off the data port (60h),
//                    and compares it with the byte in the In Field.  If the
//                    comparison fails, the request is aborted (refer to the
//                    commandsCount field in the request structure).
//    o  In Field:    Holds byte that comparison should be made to.
//
// o  kPS2C_WriteDataPort:
//    o  Description: Writes the byte in the In Field to the data port (60h).
//    o  In Field:    Holds byte that should be written.
//
// o  kPS2C_WriteCommandPort:
//    o  Description: Writes the byte in the In Field to the command port (64h).
//    o  In Field:    Holds byte that should be written.
//

enum PS2CommandEnum
{
  kPS2C_ReadDataPort,
  kPS2C_ReadDataPortAndCompare,
  kPS2C_WriteDataPort,
  kPS2C_WriteCommandPort,
  kPS2C_SendMouseCommandAndCompareAck,
  kPS2C_ReadMouseDataPort,
  kPS2C_ReadMouseDataPortAndCompare,
  kPS2C_FlushDataPort,
  kPS2C_SleepMS,
  kPS2C_ModifyCommandByte,
};
typedef enum PS2CommandEnum PS2CommandEnum;

struct PS2Command
{
  PS2CommandEnum command;
  union
  {
      UInt8  inOrOut;
      UInt32 inOrOut32;
      struct
      {
          UInt8 setBits;
          UInt8 clearBits;
          UInt8 oldBits;
      };
  };
};
typedef struct PS2Command PS2Command;



class F03 : public RMIFunction {
    OSDeclareDefaultStructors(F03)
    
public:
//    bool init(OSDictionary *dictionary) override;
    bool attach(IOService *provider) override;
//    void stop(IOService *provider) override;
//    void free() override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    
private:
    RMIBus *rmiBus;
    
    // F03 Data
    unsigned int overwrite_buttons;
    
    u8 device_count;
    u8 rx_queue_length;


};

#endif /* F03_hpp */
