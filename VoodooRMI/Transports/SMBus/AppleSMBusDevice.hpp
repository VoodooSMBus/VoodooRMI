//
//  AppleSMBusDevice.h
//  RMISMBus
//
//  Created by Sheika Slate on 6/11/21.
//  Copyright © 2021 1Revenger1. All rights reserved.
//

#ifndef AppleSMBusDevice_h
#define AppleSMBusDevice_h

#include <IOKit/IOUserClient.h>
#include <IOKit/IOSMBusController.h>

#define SMBUS_PROCESS_CALL 3
#define SMBUS_DATA_CALL 1

#define DATA_FLAG 2

#define SMBUS_THIS_DEVICE 1

#pragma pack(push, 4)
struct AppleSMBusI2CRequest {
    uint32_t sendProtocol;
    uint32_t receiveProtocol;
    uint32_t sendAddress;       // 1 == SMBus device addr
    uint32_t receiveAddress;    // 1 == SMBus device addr
    uint8_t sendSubAddress;
    uint8_t receiveSubAddress;
    uint8_t __unknown1[10];
    uint32_t sendFlags;
    uint32_t receiveFlags;
    uint8_t __unknowxn2;
    uint32_t sendBytes;
    uint8_t __unknown3[12];
    uint32_t receiveBytes;
    uint8_t __unknown4[8];
    uint8_t *buffer; // max size 32
    uint8_t *receieveBuffer;
};
#pragma pack(pop)


class AppleSMBusDevice : public IOService {
    OSDeclareDefaultStructors(AppleSMBusDevice)
public:
    
    virtual bool start (IOService *) override;
    virtual void stop (IOService *) override;
    
    virtual bool terminate (uint) override;
    
    virtual void newUserClient (task_t *, void *, uint, OSDictionary *, IOUserClient **);
    virtual void newUserClient (task_t *, void *, uint32_t, IOUserClient **);
    
    IOReturn startIO (AppleSMBusI2CRequest *);
    
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 0);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 1);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 2);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 3);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 4);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 5);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 6);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 7);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 8);
    OSMetaClassDeclareReservedUnused(AppleSMBusDevice, 9);
};

#endif /* AppleSMBusDevice_h */
