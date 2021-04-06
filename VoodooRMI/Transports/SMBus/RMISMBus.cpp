/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_smbus.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMISMBus.hpp"

OSDefineMetaClassAndStructors(RMISMBus, RMITransport)
#define super IOService

bool RMISMBus::init(OSDictionary *dictionary)
{
    page_mutex = IOLockAlloc();
    mapping_table_mutex = IOLockAlloc();
    memset(mapping_table, 0, sizeof(mapping_table));
    return super::init(dictionary);
}

RMISMBus *RMISMBus::probe(IOService *provider, SInt32 *score)
{
    if (!super::probe(provider, score)) {
        IOLogError("Failed probe");
        return NULL;
    }
    
    device_nub = OSDynamicCast(VoodooSMBusDeviceNub, provider);
    if (!device_nub) {
        IOLogError("Could not cast nub");
        return NULL;
    }
    
    device_nub->wakeupController();
    device_nub->setSlaveDeviceFlags(I2C_CLIENT_HOST_NOTIFY);
    
    return this;
}

bool RMISMBus::start(IOService *provider)
{
    auto dict = IOService::nameMatching("ApplePS2SynapticsTouchPad");
    if (!dict) {
        IOLogError("Unable to create name matching dictionary");
        return false;
    }
    
    
    // Do a reset over PS2 if possible
    // If ApplePS2Synaptics isn't there, we can *likely* assume that they did not inject VoodooPS2Trackpad
    // In which case, resetting isn't important unless it's a broken HP machine
    auto ps2 = waitForMatchingService(dict, UInt64 (5) * kSecondScale);
    
    if (ps2) {
        // VoodooPS2Trackpad is currently initializing.
        // We don't know what state it is in, so wait for registerService()
        IOLogInfo("Found PS2 Trackpad driver! Waiting for registerService()");
        
        IONotifier *notifierStatus = addMatchingNotification(gIOMatchedNotification,
                                                             dict, 0,
                                                             ^bool (IOService *newService, IONotifier *notifier) {
            if (!newService->getProperty("VoodooInputSupported")) {
                IOLogDebug("Too early notification - No VoodooInput on PS2 yet");
                return true;
            }
            
            if (OSObject *gpio = newService->getProperty("GPIO Data")) {
                IOLogDebug("Found GPIO data!");
                setProperty("GPIO Data", gpio);
            }
            
            IOLogInfo("VoodooPS2Trackpad finished init, starting...");
            messageClient(kPS2M_SMBusStart, newService);
            notifier->remove();
            rmiStart();
            
            return true;
        });
        
        if (!notifierStatus) {
            IOLogError("Notifier not installed");
        }
        
        // Retained by addMatchingNotification
        OSSafeReleaseNULL(dict);
        OSSafeReleaseNULL(ps2);
        return !!notifierStatus;
    }
    
    OSSafeReleaseNULL(dict);
    
    // No VoodooPS2Trackpad, start now
    return rmiStart();
}

bool RMISMBus::rmiStart()
{
    int retval = 0, attempts = 0;
    
    do {
        retval = rmi_smb_get_version();
        IOSleep(500);
    } while (retval < 0 && attempts++ < 5);
    
    if (retval < 0) {
        IOLogError("Error: Failed to read SMBus version. Code: 0x%02X", retval);
        return false;
    }
    
    if (retval != 2 && retval != 3) {
        IOLogError("Unrecognized SMB Version %d", retval);
        return false;
    }
    
    setProperty("SMBus Version", retval, 32);
    IOLogInfo("SMBus version %u", retval);
    
    PMinit();
    device_nub->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);
    
    setProperty(RMIBusSupported, kOSBooleanTrue);
    registerService();
    return true;
}

void RMISMBus::stop(IOService *provider)
{
    PMstop();
    super::stop(provider);
}

void RMISMBus::free()
{
    if (page_mutex)
        IOLockFree(page_mutex);
    if (mapping_table_mutex)
        IOLockFree(mapping_table_mutex);
    super::free();
}

int RMISMBus::rmi_smb_get_version()
{
    int retval;
    
    /* Check for SMBus new version device by reading version byte. */
    retval = device_nub->readByteData(SMB_PROTOCOL_VERSION_ADDRESS);
    if (retval < 0) {
        return retval;
    }
    
    return retval + 1;
}

int RMISMBus::reset()
{
    /* Discard mapping table */
    IOLockLock(mapping_table_mutex);
    memset(mapping_table, 0, sizeof(mapping_table));
    IOLockUnlock(mapping_table_mutex);

    // Full reset can only be done in PS2
    // Getting the version allows the trackpad to be used over SMBus
    return rmi_smb_get_version();
}

/*
 * The function to get command code for smbus operations and keeps
 * records to the driver mapping table
 */
int RMISMBus::rmi_smb_get_command_code(u16 rmiaddr, int bytecount,
                                       bool isread, u8 *commandcode)
{
    struct mapping_table_entry new_map;
    u8 i;
    int retval = 0;
    
    IOLockLock(mapping_table_mutex);
    
    for (i = 0; i < RMI_SMB2_MAP_SIZE; i++) {
        struct mapping_table_entry *entry = &mapping_table[i];
        
        if (OSSwapLittleToHostInt16(entry->rmiaddr) == rmiaddr) {
            if (isread) {
                if (entry->readcount == bytecount)
                    goto exit;
            } else {
                if (entry->flags & RMI_SMB2_MAP_FLAGS_WE) {
                    goto exit;
                }
            }
        }
    }
    
    i = table_index;
    table_index = (i + 1) % RMI_SMB2_MAP_SIZE;
    
    /* constructs mapping table data entry. 4 bytes each entry */
    memset(&new_map, 0, sizeof(new_map));
    new_map.rmiaddr = OSSwapHostToLittleInt16(rmiaddr);
    new_map.readcount = bytecount;
    new_map.flags = !isread ? RMI_SMB2_MAP_FLAGS_WE : 0;
    retval = device_nub->writeBlockData(i + 0x80,
                                         sizeof(new_map), reinterpret_cast<u8*>(&new_map));
    if (retval < 0) {
        IOLogError("smb_get_command_code: Failed to write mapping table data");
        /*
         * if not written to device mapping table
         * clear the driver mapping table records
         */
        memset(&new_map, 0, sizeof(new_map));
    }
    
    /* save to the driver level mapping table */
    mapping_table[i] = new_map;
    
exit:
    IOLockUnlock(mapping_table_mutex);
    
    if (retval < 0)
        return retval;
    
    *commandcode = i;
    return 0;
}

int RMISMBus::readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
    int retval;
    u8 commandcode;
    int cur_len = (int)len;
    
    IOLockLock(page_mutex);
    memset(databuff, 0, len);
    
    while (cur_len > 0) {
        /* break into 8 bytes chunks to write get command code */
        int block_len =  min(cur_len, SMB_MAX_COUNT);
        
        retval = rmi_smb_get_command_code(rmiaddr, block_len,
                                          true, &commandcode);
        if (retval < 0)
            goto exit;
        
        retval = device_nub->readBlockData(commandcode, databuff);
        
        if (retval < 0)
            goto exit;
        
        /* prepare to read next block of bytes */
        cur_len -= SMB_MAX_COUNT;
        databuff += SMB_MAX_COUNT;
        rmiaddr += SMB_MAX_COUNT;
    }
    
    retval = 0;
    
exit:
    IOLockUnlock(page_mutex);
    return retval;
}

int RMISMBus::blockWrite(u16 rmiaddr, u8 *buf, size_t len)
{
    int retval = 0;
    u8 commandcode;
    int cur_len = (int)len;
    
    IOLockLock(page_mutex);
    
    while (cur_len > 0) {
        /*
         * break into 32 bytes chunks to write get command code
         */
        int block_len = min(cur_len, SMB_MAX_COUNT);
        
        retval = rmi_smb_get_command_code(rmiaddr, block_len,
                                          false, &commandcode);
        
        if (retval < 0)
            goto exit;
        
        retval = device_nub->writeBlockData(commandcode,
                                            block_len, buf);
        
        if (retval < 0)
            goto exit;
        
        cur_len -= SMB_MAX_COUNT;
        buf += SMB_MAX_COUNT;
    }
    
exit:
    IOLockUnlock(page_mutex);
    return retval;
}

IOReturn RMISMBus::message(UInt32 type, IOService *provider, void *argument) {
    if (!bus) return kIOReturnError;
    
    switch (type) {
        case kIOMessageVoodooSMBusHostNotify:
            return messageClient(kIOMessageVoodooSMBusHostNotify, bus);
        default:
            return IOService::message(type, provider, argument);
    }
};

IOReturn RMISMBus::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOPMAckImplied;
    
    if (whichState == 0) {
        messageClient(kIOMessageRMI4Sleep, bus);
    } else {
        // FIXME: Hardcode 1s sleep delay because device will otherwise time out during reconfig
        IOSleep(1000);
        
        // Put trackpad in SMBus mode again
        int retval = reset();
        if (retval < 0) {
            IOLogError("Failed to reset trackpad!");
            return kIOPMAckImplied;
        }
        
        // Reconfigure device
        retval = messageClient(kIOMessageRMI4ResetHandler, bus);
        if (retval < 0) {
            IOLogError("Failed to config trackpad!");
            return kIOPMAckImplied;
        }
        
        // Enable trackpad again
        retval = messageClient(kIOMessageRMI4Resume, bus);
        if (retval < 0) {
            IOLogError("Failed to resume trackpad!");
            return kIOPMAckImplied;
        }
    }

    return kIOPMAckImplied;
}
