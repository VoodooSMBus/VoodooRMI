/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_smbus.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOCatalogue.h>
#include "RMISMBus.hpp"
#include "RMIMessages.h"
#include "RMIConfiguration.hpp"
#include "RMIPowerStates.h"
#include "RMILogging.h"

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

bool RMISMBus::acidantheraTrackpadExists() {
    SInt32 generation;
    
    OSDictionary *trackpadDict = IOService::serviceMatching("ApplePS2SynapticsTouchPad");
    if (trackpadDict == nullptr) {
        IOLogError("Unable to create service matching dictionary");
        return false;
    }
    
    OSOrderedSet *personalities = gIOCatalogue->findDrivers(trackpadDict, &generation);
    if (personalities == nullptr) {
        OSSafeReleaseNULL(trackpadDict);
        IOLogError("Error retrieving PS2 personalities");
        return false;
    }
    
    OSDictionary *personality = OSDynamicCast(OSDictionary, personalities->getFirstObject());
    if (personality == nullptr) {
        OSSafeReleaseNULL(personalities);
        OSSafeReleaseNULL(trackpadDict);
        IOLogError("No ApplePS2SynapticsTouchPad personality found");
        return false;
    }
    
    OSString *bundle = OSDynamicCast(OSString, personality->getObject(gIOModuleIdentifierKey));
    
    bool ret = bundle != nullptr &&
               bundle->isEqualTo("as.acidanthera.voodoo.driver.PS2Trackpad");
    
    OSSafeReleaseNULL(personalities);
    OSSafeReleaseNULL(trackpadDict);
    return ret;
}

bool RMISMBus::makePS2DriverBowToUs() {
    // Find PS2 Controller and Synpatics Trackpad PS/2 Driver
    auto trackpadDict = IOService::serviceMatching("ApplePS2SynapticsTouchPad");
    auto ps2contDict = IOService::serviceMatching("ApplePS2Controller");
    
    if (trackpadDict == nullptr || ps2contDict == nullptr) {
        IOLogError("Unable to create service matching dictionaries");
        return false;
    }
    
    IOService *ps2Controller = waitForMatchingService(ps2contDict);
    IOService *ps2Trackpad = waitForMatchingService(trackpadDict);
    
    if (ps2Trackpad == nullptr || ps2Controller == nullptr) {
        IOLogError("Could not find PS2 Trackpad driver! Aborting...");
        OSSafeReleaseNULL(ps2Trackpad);
        OSSafeReleaseNULL(ps2Controller);
        return false;
    }
    
    // Grab any useful information from Trackpad driver
    OSObject *gpio = ps2Trackpad->getProperty("GPIO Data");
    if (gpio != nullptr) {
        IOLogDebug("Found GPIO data!");
        setProperty("GPIO Data", gpio);
    }
    
    // Register for power notifications so we know when it's safe to reinit over SMBus
    ps2Controller->registerInterestedDriver(this);
    
    IOService *ps2Nub = ps2Trackpad->getProvider();
    if (ps2Nub == nullptr) {
        OSSafeReleaseNULL(ps2Trackpad);
        OSSafeReleaseNULL(ps2Controller);
        return false;
    }
    
    // Grab port number for trackpad so we can tell the controller the right driver to kill
    OSNumber *ps2PortNum = OSDynamicCast(OSNumber, ps2Nub->getProperty("Port Num"));
    if (ps2PortNum == nullptr) {
        OSSafeReleaseNULL(ps2Trackpad);
        OSSafeReleaseNULL(ps2Controller);
        return false;
    }
    
    // Kill PS/2 driver and replace it with a stub to do our own bidding (heheheh)
    const OSSymbol *funcName = OSSymbol::withCString("PS2CreateSMBusStub");
    IOReturn ret = ps2Controller->callPlatformFunction(funcName,
                                                       true,
                                                       reinterpret_cast<void *>(ps2PortNum->unsigned8BitValue()),
                                                       nullptr,
                                                       nullptr,
                                                       nullptr);
    
    OSSafeReleaseNULL(funcName);
    OSSafeReleaseNULL(ps2Trackpad);
    OSSafeReleaseNULL(ps2Controller);
    return ret == kIOReturnSuccess;
}

bool RMISMBus::start(IOService *provider)
{
    int version;
    
    if (!acidantheraTrackpadExists()) {
        IOLogError("Acidanthera ApplePS2SynapticsTouchPad does not exist!");
        return false;
    }
    
    // Do a reset over PS2, replace the PS2 Synaptics Driver with a stub driver, and get GPIO data
    if (!makePS2DriverBowToUs()) {
        IOLogError("Failed to disable PS2 Trackpad");
        return false;
    }
    
    version = rmi_smb_get_version();
    if (version < 0) {
        IOLogError("Error: Failed to read SMBus version. Code: 0x%02X", version);
        return false;
    }
    
    if (version != 2 && version != 3) {
        IOLogError("Unrecognized SMB Version %d", version);
        return false;
    }
    
    setProperty("SMBus Version", version, 32);
    IOLogInfo("SMBus version %u", version);
    
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
int RMISMBus::rmi_smb_get_command_code(UInt16 rmiaddr, int bytecount,
                                       bool isread, UInt8 *commandcode)
{
    struct mapping_table_entry new_map;
    UInt8 i;
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
                                         sizeof(new_map), reinterpret_cast<UInt8*>(&new_map));
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

int RMISMBus::readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) {
    int retval;
    UInt8 commandcode;
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

int RMISMBus::blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len)
{
    int retval = 0;
    UInt8 commandcode;
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
        
        // Reconfigure and enable trackpad again
        retval = messageClient(kIOMessageRMI4Resume, bus);
        if (retval < 0) {
            IOLogError("Failed to resume trackpad!");
            return kIOPMAckImplied;
        }
    }

    return kIOPMAckImplied;
}

OSDictionary *RMISMBus::createConfig() {
    OSDictionary *config = nullptr;
    OSArray *acpiArray = nullptr;
    OSObject *acpiReturn = nullptr;
    
    // Unlike VoodooI2C, VoodooSMBusNubs are not ACPI Devices directly. Grab the device that the controller is attached too
    IORegistryEntry *controller = device_nub->getParentEntry(gIOServicePlane);
    if (controller == nullptr) return nullptr;
    
    IORegistryEntry *pciNub = controller->getParentEntry(gIOServicePlane);
    if (pciNub == nullptr || pciNub->getProperty("acpi-device") == nullptr) {
        IOLogError("%s Could not retrieve controller for config", getName());
        return nullptr;
    }
    
    IOACPIPlatformDevice *acpi_device = OSDynamicCast(IOACPIPlatformDevice, pciNub->getProperty("acpi-device"));
    if (!acpi_device) {
        IOLogError("%s Could not retrieve acpi device", getName());
        return nullptr;
    };

    if (acpi_device->evaluateObject("RCFG", &acpiReturn) != kIOReturnSuccess) {
        return nullptr;
    }
    
    acpiArray = OSDynamicCast(OSArray, acpiReturn);
    config = Configuration::mapArrayToDict(acpiArray);
    OSSafeReleaseNULL(acpiReturn);
    
    return config;
}

IOReturn RMISMBus::powerStateWillChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService *whatDevice) {
    return kIOPMAckImplied;
}

IOReturn RMISMBus::powerStateDidChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService *whatDevice) {
    // Do stuff here. Only reinit when PS/2 is ready and SMBus is ready
    return kIOPMAckImplied;
}
