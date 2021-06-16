#ifndef AppleSMBusDevice_h
#define AppleSMBusDevice_h

#include <IOKit/IOUserClient.h>
#include <IOKit/IOSMBusController.h>

// Transaction Type
enum {
    kAppleSMBusI2CSimpleTransactionType = 1,
    kAppleSMbusI2CProcessTransactionType = 3,
};

// Transaction Flags
enum {
    kAppleSMBusI2CDataFlag = 0x00000002,
};

static constexpr uint32_t kAppleSMBusI2CUseNubAddress = 1;

#pragma pack(push, 4)
struct AppleSMBusI2CRequest {
    uint32_t sendTransactionType;
    uint32_t receiveTransactionType;
    uint32_t sendAddress;
    uint32_t receiveAddress;
    uint8_t sendSubAddress;
    uint8_t receiveSubAddress;
    uint8_t __unknown1[10];
    uint32_t result;            // Never used
    uint32_t transactionFlags;
    uint8_t __unknown2[4];
    uint32_t sendBytes;
    uint8_t __unknown3[12];
    uint32_t receiveBytes;
    void * callback;
    // Max buffer size is 32 bytes
    uint8_t *sendBuffer;
    uint8_t *receievBuffer;
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
