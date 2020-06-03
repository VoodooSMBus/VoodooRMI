/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_smbus.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMITransport.hpp"

OSDefineMetaClassAndStructors(RMITransport, IOService)
OSDefineMetaClassAndStructors(RMISMBus, RMITransport)
#define super IOService

bool RMISMBus::init(OSDictionary *dictionary)
{
    page_mutex = IOLockAlloc();
    mapping_table_mutex = IOLockAlloc();
    return super::init(dictionary);
}

RMISMBus *RMISMBus::probe(IOService *provider, SInt32 *score)
{
    IOService *service = super::probe(provider, score);
    if(!service) {
        IOLog("Failed probe");
        return NULL;
    }
//    return OSDynamicCast(RMISMBus, service);
    
    device_nub = OSDynamicCast(VoodooSMBusDeviceNub, provider);
    if (!device_nub) {
        IOLog("Could not cast nub\n");
        return NULL;
    }
    
    device_nub->setSlaveDeviceFlags(I2C_CLIENT_HOST_NOTIFY);
    
    int retval = rmi_smb_get_version();
    if (retval < 0) {
        IOLog("Error: Failed to read SMBus version. Code: 0x%02X", retval);
        return NULL;
    }
    
    IOLog("SMBus version %u\n", retval);
    
    return OSDynamicCast(RMISMBus, service);
}

bool RMISMBus::start(IOService *provider)
{
    bool res = super::start(provider);
    registerService();
    return res;
}

void RMISMBus::free()
{
    IOLockFree(page_mutex);
    IOLockFree(mapping_table_mutex);
    super::free();
}

int RMISMBus::rmi_smb_get_version()
{
    int retval;
    
    /* Check if for SMBus new version device by reading version byte. */
    retval = device_nub->readByteData(SMB_PROTOCOL_VERSION_ADDRESS);
    if (retval < 0) {
        IOLog("Failed to get SMBus version number!\n");
        return retval;
    }
    
    return retval + 1;
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

int RMISMBus::read(u16 rmiaddr, u8 *databuff) {
    return readBlock(rmiaddr, databuff, 1);
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

int RMISMBus::write(u16 rmiaddr, u8 *buf) {
    return blockWrite(rmiaddr, buf, 1);
}

