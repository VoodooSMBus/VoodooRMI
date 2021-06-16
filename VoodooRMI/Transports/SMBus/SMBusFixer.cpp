/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#include "SMBusFixer.hpp"

OSDefineMetaClassAndStructors(SMBusFixer, IOService)
#define super IOService

IOService *SMBusFixer::probe(IOService *provider, SInt32 *score) {
    pci_device = OSDynamicCast (IOPCIDevice, provider);
    
    if (!pci_device) {
        return NULL;
    }
    
    uint32_t host_config = pci_device->configRead8(SMBHSTCFG);
    if ((host_config & SMBHSTCFG_HST_EN) == 0) {
        return NULL;
    }
    
    base_addr = pci_device->configRead16(ICH_SMB_BASE) & 0xFFFE;
    
    IOLog("VRMI - %x Aux Ctl\n", pci_device->ioRead8(SMBAUXCTL(base_addr)));
    IOLog("VRMI - %x Status\n", pci_device->ioRead8(base_addr));
    
    return this;
};

bool SMBusFixer::start(IOService *provider) {
    pci_device->ioWrite8(SMBAUXCTL(base_addr), 0b00000010);
    IOLog("VRMI - SMBusFixer Start - %x Aux Ctl\n", pci_device->ioRead8(SMBAUXCTL(base_addr)));
    
    return super::start(provider);
}
