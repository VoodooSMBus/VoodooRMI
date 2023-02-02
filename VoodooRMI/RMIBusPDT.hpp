//
//  RMIBusPDT.hpp
//  VoodooRMI
//
//  Created by Gwydien on 1/31/23.
//  Copyright © 2023 1Revenger1. All rights reserved.
//

#ifndef RMIBusPDT_hpp
#define RMIBusPDT_hpp

#include <IOKit/IOLib.h>

#define NAME_BUFFER_SIZE 256

// IRQs
#define RMI_MAX_IRQS 32

// Page Description Table
#define RMI_PAGE_MASK 0xFF00
#define RMI_MAX_PAGE 0xFF
#define RMI_PDT_START 0xE9
#define RMI_PDT_STOP 0x5

// PDT entry data directly from RMI4 device
struct RmiPdtData {
    UInt8 functionNum;
    UInt8 interruptBits : 3;
    UInt8 _ : 2;
    UInt8 functionVersion : 1;
    UInt8 __ : 1;
    UInt8 dataBase;
    UInt8 ctrlBase;
    UInt8 cmdBase;
    UInt8 qryBase;
};

// Parsed PDT data
struct RmiPdtEntry {
    UInt16 dataAddr;
    UInt16 ctrlAddr;
    UInt16 cmdAddr;
    UInt16 qryAddr;
    UInt8 function;
    UInt8 interruptBits;
    UInt32 irqMask;
};


#endif /* RMIBusPDT_hpp */
