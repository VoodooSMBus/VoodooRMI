/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Power States
 *
 * Copyright (c) 2023 Avery Black
 */

#ifndef RMIPowerStates_h
#define RMIPowerStates_h

#define RMI_POWER_OFF 0
#define RMI_POWER_ON 1
static IOPMPowerState RMIPowerStates[] = {
    {1, 0                , 0, 0           , 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

#endif /* RMIPowerStates_h */
