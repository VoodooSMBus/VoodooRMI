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
