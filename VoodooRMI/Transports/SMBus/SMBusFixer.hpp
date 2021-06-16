/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#ifndef SMBusFixer_hpp
#define SMBusFixer_hpp

#include <IOKit/pci/IOPCIDevice.h>

#define SMBHSTCFG           0x40
#define SMBHSTCFG_HST_EN    0x1
#define ICH_SMB_BASE        0x20

#define SMBAUXCTL(p)    (13 + p)

class SMBusFixer : public IOService {
    OSDeclareDefaultStructors(SMBusFixer)
public:
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    virtual bool start(IOService *provider) override;
private:
    uint16_t base_addr{0};
    IOPCIDevice *pci_device{nullptr};
};

#endif /* SMBusFixer_hpp */
