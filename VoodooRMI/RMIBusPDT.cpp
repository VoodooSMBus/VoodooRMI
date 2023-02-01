//
//  RMIBusPDT.cpp
//  VoodooRMI
//
//  Created by Gwydien on 1/31/23.
//  Copyright © 2023 1Revenger1. All rights reserved.
//

#include "RMIBusPDT.hpp"
#include <RMIBus.hpp>
#include <F01.hpp>
#include <F03.hpp>
#include <F11.hpp>
#include <F12.hpp>
#include <F17.hpp>
#include <F30.hpp>
#include <F3A.hpp>

/*
 * The Page description table describes all of the functions/capabilities of
 *  the RMI4 device. Each function is represented as an entry in this table,
 *  and each "page" (256 block) can contain any number of these entries.
 * Most devices only contain 1-2 pages worth of functions, and Function 1
 *  should always be found on the first page.
 * All the function services should be instantiated in here, and IRQ bits
 *  should be counted so we know how many IRQ registers there are.
 */
IOReturn RMIBus::rmiScanPdt() {
    int blankPages = 0;
    RmiPdtEntry pdtEntry;
    
    for (UInt16 page = 0; page <= RMI_MAX_PAGE; page++) {
        UInt16 pageBase = page * 0x100;
        UInt8 offset;
        
        for (offset = RMI_PDT_START; offset >= RMI_PDT_STOP; offset -= sizeof(RmiPdtData)) {
            IOReturn ret = rmiReadPdtEntry(pdtEntry, pageBase + offset);
            
            if (ret != kIOReturnSuccess) {
                return ret;
            }
            
            if (pdtEntry.function == 0 || pdtEntry.function == 0xFF) {
                // End of descriptors for this page
                break;
            }
        }
        
        // Look for 2 consecutive blank pages before ending scan
        blankPages = (offset == RMI_PDT_START) ? blankPages + 1 : 0;
        if (blankPages >= 2) {
            break;
        }
    }
    
    return kIOReturnSuccess;
}

IOReturn RMIBus::rmiReadPdtEntry(RmiPdtEntry &entry, UInt16 addr) {
    RmiPdtData data;

    IOReturn readRet = readBlock(addr, reinterpret_cast<UInt8 *>(&data), sizeof(RmiPdtData));
    if (readRet < 0) {
        IOLogError("Failed to read description table entry!");
        return readRet;
    }
    
    UInt16 pageBase = addr & RMI_PAGE_MASK;
    entry.function = data.functionNum;
    entry.interruptBits = data.interruptBits;
    entry.cmdAddr = pageBase + data.cmdBase;
    entry.ctrlAddr = pageBase + data.ctrlBase;
    entry.dataAddr = pageBase + data.dataBase;
    entry.qryAddr = pageBase + data.qryBase;
    return kIOReturnSuccess;
}

/*
 * Instantiate functions and count IRQs
 */
IOReturn RMIBus::rmiHandlePdtEntry(RmiPdtEntry &entry) {
    RMIFunction * function;
    
    irqMask |= ((1 << entry.interruptBits) - 1) << irqCount;
    irqCount += entry.interruptBits;
    if (irqCount > RMI_MAX_IRQS) {
        IOLogError("Too many IRQ bits!");
        return kIOReturnNoSpace;
    }

    IOLogDebug("Function F%X - IRQs: %u CMD Base: 0x%x CTRL Base: 0x%x DATA Base: 0x%x QRY Base: 0x%x",
               entry.function,
               entry.interruptBits,
               entry.cmdAddr,
               entry.ctrlAddr,
               entry.dataAddr,
               entry.qryAddr
               );
    
    switch(entry.function) {
        case 0x01: /* device control */
            function = OSTypeAlloc(F01);
            break;
        case 0x03: /* PS/2 pass-through */
            function = OSTypeAlloc(F03);
            break;
        case 0x11: /* multifinger pointing */
            function = OSTypeAlloc(F11);
            break;
        case 0x12: /* multifinger pointing */
            function = OSTypeAlloc(F12);
            break;
        case 0x17: /* trackpoints */
            function = OSTypeAlloc(F17);
            break;
        case 0x30: /* GPIO and LED controls */
            function = OSTypeAlloc(F30);
            break;
        case 0x3A: /* Buttons? */
            function = OSTypeAlloc(F3A);
            break;
//        case 0x08: /* self test (aka BIST) */
//        case 0x09: /* self test (aka BIST) */
//        case 0x17: /* trackpoints */
//        case 0x19: /* capacitive buttons */
//        case 0x1A: /* simple capacitive buttons */
//        case 0x21: /* force sensing */
//        case 0x32: /* timer */
        case 0x34: /* device reflash */
//        case 0x36: /* auxiliary ADC */
//        case 0x41: /* active pen pointing */
        case 0x54: /* analog data reporting */
        case 0x55: /* Sensor tuning */
            IOLogInfo("F%X not implemented", entry.function);
            return kIOReturnSuccess;
        default:
            IOLogError("Unknown function: %02X - Continuing to load", entry.function);
            return kIOReturnSuccess;
    }

    if (!function || !function->init(entry)) {
        IOLogError("Could not initialize function: %02X", entry.function);
        OSSafeReleaseNULL(function);
        return kIOReturnNoDevice;
    }
    
    if (!function->attach(this)) {
        IOLogError("Function %02X could not attach", entry.function);
        OSSafeReleaseNULL(function);
        return kIOReturnNoDevice;
    }
    
    if (OSDynamicCast(RMITrackpadFunction, function)) {
        trackpadFunction = function;
    } else if (OSDynamicCast(RMITrackpointFunction, function)) {
        trackpointFunction = function;
    }
    
    functions->setObject(function);
    OSSafeReleaseNULL(function);
    return kIOReturnSuccess;
}
