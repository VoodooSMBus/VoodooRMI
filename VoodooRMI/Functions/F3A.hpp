//
//  F3A.hpp
//  VoodooRMI
//
//  Created by Avery Black on 9/20/20.
//  Copyright © 2020 1Revenger1. All rights reserved.
//

#ifndef F3A_hpp
#define F3A_hpp

#include <RMIBus.hpp>

class F3A : public RMIFunction {
    OSDeclareDefaultStructors(F3A)
    
public:
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
//    void stop(IOService *providerr) override;
//    void free() override;
//    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
//    void handleClose(IOService *forClient, IOOptionBits options) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;

private:
    RMIBus *rmiBus;
};

#endif /* F3A_hpp */
