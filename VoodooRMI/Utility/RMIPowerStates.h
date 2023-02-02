//
//  RMIPowerStates.h
//  VoodooRMI
//
//  Created by Gwydien on 2/1/23.
//  Copyright © 2023 1Revenger1. All rights reserved.
//

#ifndef RMIPowerStates_h
#define RMIPowerStates_h

#define RMI_POWER_OFF 0
#define RMI_POWER_ON 1
static IOPMPowerState RMIPowerStates[] = {
    {1, 0                , 0, 0           , 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

#endif /* RMIPowerStates_h */
