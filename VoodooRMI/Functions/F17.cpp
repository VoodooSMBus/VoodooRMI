/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
 * Ported to macOS from linux kernel for Tegra on Android, original source at
 https://android.googlesource.com/kernel/tegra/+/android-7.1.1_r0.79/drivers/input/touchscreen/rmi4/rmi_f17.c
 *
 * Copyright (c) 2012 Synaptics Incorporated
 */

#include "F17.hpp"

OSDefineMetaClassAndStructors(F17, RMIFunction)
#define super IOService

bool F17::init(OSDictionary *dictionary)
{
    if (!super::init(dictionary))
        return false;
    
    f17 = reinterpret_cast<rmi_f17_device_data*>(IOMallocZero(sizeof(rmi_f17_device_data)));
    if (!f17) {
        IOLogError("%s: Failed to allocate function data", __func__);
        return false;
    }

    return true;
}

bool F17::attach(IOService *provider)
{
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F30: No provider.");
        return false;
    }
    
    int retval = rmi_f17_initialize();
    
    if (retval < 0)
        return false;
    
    super::attach(provider);
    
    return true;
}

bool F17::start(IOService *provider)
{
    if (!super::start(provider))
        return false;
    
    int ret = rmi_f17_config();
    if (ret < 0) return false;
    
    voodooTrackpointInstance = rmiBus->getVoodooInput();
    registerService();
    return super::start(provider);
}

void F17::free()
{
    clearDesc();

    if (f17) {
        if (f17->sticks)
            IOFree(f17->sticks, (f17->query.number_of_sticks+1) * sizeof(struct rmi_f17_stick_data));
        IOFree(f17, sizeof(rmi_f17_device_data));
    }
    super::free();
}

IOReturn F17::message(UInt32 type, IOService *provider, void *argument)
{
    int retval = 0;
    switch (type) {
        case kHandleRMIAttention:
            for (int i = 0; i < f17->query.number_of_sticks + 1 && !retval; i++)
                retval = rmi_f17_process_stick(&f17->sticks[i]);
            
            if (retval < 0) {
                IOLogError("%s: Could not read data: 0x%x", __func__, retval);
            }
            
            break;
        case kHandleRMIConfig:
            return rmi_f17_config();
    }
    
    return kIOReturnSuccess;
}

int F17::rmi_f17_init_stick(struct rmi_f17_stick_data *stick,
              u16 *next_query_reg, u16 *next_data_reg,
              u16 *next_control_reg) {
    int retval = 0;
    retval = rmiBus->readBlock(*next_query_reg,
        stick->query.general.regs,
        sizeof(stick->query.general.regs));
    if (retval < 0) {
        IOLogError("%s: Failed to read stick general query", __func__);
        return retval;
    }
    *next_query_reg += sizeof(stick->query.general.regs);
    IOLogDebug("%s: Stick %d found", __func__, stick->index);
    IOLogDebug("%s:     Manufacturer: %d", __func__, stick->query.general.manufacturer);
    IOLogDebug("%s:     Resistive:    %d", __func__, stick->query.general.resistive);
    IOLogDebug("%s:     Ballistics:   %d", __func__, stick->query.general.ballistics);
    IOLogDebug("%s:     Manufacturer: %d", __func__, stick->query.general.ballistics);
    IOLogDebug("%s:     Has relative: %d", __func__, stick->query.general.has_relative);
    IOLogDebug("%s:     Has absolute: %d", __func__, stick->query.general.has_absolute);
    IOLogDebug("%s:     Had dribble:  %d", __func__, stick->query.general.has_dribble);
    IOLogDebug("%s:     Has gestures: %d", __func__, stick->query.general.has_gestures);
    if (stick->query.general.has_gestures) {
        retval = rmiBus->readBlock(*next_query_reg,
            stick->query.gestures.regs,
            sizeof(stick->query.gestures.regs));
        if (retval < 0) {
            IOLogError("%s: Failed to read F17 gestures query, code %d", __func__, retval);
            return retval;
        }
        *next_query_reg += sizeof(stick->query.gestures.regs);
        IOLogDebug("%s:         single tap: %d", __func__, stick->query.gestures.has_single_tap);
        IOLogDebug("%s:         tap & hold: %d", __func__, stick->query.gestures.has_tap_and_hold);
        IOLogDebug("%s:         double tap: %d", __func__, stick->query.gestures.has_double_tap);
        IOLogDebug("%s:         early tap:  %d", __func__, stick->query.gestures.has_early_tap);
        IOLogDebug("%s:         press:      %d", __func__, stick->query.gestures.has_press);
    }
    if (stick->query.general.has_absolute) {
        stick->data.abs.address = *next_data_reg;
        *next_data_reg += sizeof(stick->data.abs.regs);
    }
    if (stick->query.general.has_relative) {
        stick->data.rel.address = *next_data_reg;
        *next_data_reg += sizeof(stick->data.rel.regs);
    }
    if (stick->query.general.has_gestures) {
        stick->data.gestures.address = *next_data_reg;
        *next_data_reg += sizeof(stick->data.gestures.regs);
    }
    return retval;
}

int F17::rmi_f17_initialize() {
//    u8 buf[RMI_F17_QUERY_SIZE];
    int retval;
    u16 next_query_reg = fn_descriptor->query_base_addr;
    u16 next_data_reg = fn_descriptor->data_base_addr;
    u16 next_control_reg = fn_descriptor->control_base_addr;

    retval = rmiBus->readBlock(fn_descriptor->query_base_addr,
                               f17->query.regs, sizeof(f17->query.regs));

//    retval = rmiBus->readBlock(fn_descriptor->query_base_addr,
//                              buf, RMI_F17_QUERY_SIZE);

    if (retval < 0) {
        IOLogError("%s: Failed to read query register", __func__);
        return retval;
    }

    //    numSticks = buf[0] & RMI_F17_NUM_OF_STICKS;

    IOLogInfo("%s: Found %d sticks", __func__, f17->query.number_of_sticks + 1);

    f17->sticks = reinterpret_cast<rmi_f17_stick_data*>(IOMallocZero((f17->query.number_of_sticks+1) * sizeof(struct rmi_f17_stick_data)));
    if (!(f17->sticks)) {
        IOLogError("%s: Failed to allocate per stick data", __func__);
        return -1;
    }

    next_query_reg += sizeof(f17->query.regs);

    retval = rmiBus->readBlock(fn_descriptor->command_base_addr,
                               f17->commands.regs, sizeof(f17->commands.regs));

    if (retval < 0) {
        IOLogError("%s: Failed to read command register", __func__);
        return retval;
    }

//    unused
//    f17->control_address = fn_descriptor->control_base_addr;
//    retval = f17_read_control_parameters(rmi_dev, f17);
//    if (retval < 0) {
//        IOLogError("F17: Failed to initialize control params");
//        return retval;
//    }

    for (int i = 0; i < f17->query.number_of_sticks + 1; i++) {
        f17->sticks[i].index = i;
        retval = rmi_f17_init_stick(&f17->sticks[i],
                    &next_query_reg, &next_data_reg,
                    &next_control_reg);
        if (retval < 0) {
            IOLogError("%s: Failed to init stick %d", __func__, i);
            return retval;
        }
    }

    OSNumber *value;
    OSDictionary * attribute = OSDictionary::withCapacity(1);
    setPropertyNumber(attribute, "number_of_sticks", f17->query.number_of_sticks + 1, 8);
    setProperty("Attibute", attribute);
    OSSafeReleaseNULL(attribute);

    return retval;
}

int F17::rmi_f17_config() {
    int retval = rmiBus->blockWrite(fn_descriptor->control_base_addr,
                                   f17->controls.regs, sizeof(f17->controls.regs));
    
    if (retval < 0) {
        IOLogError("%s: Could not write stick control registers at 0x%x: 0x%x",
                   __func__, fn_descriptor->control_base_addr, retval);
    }
    return retval;
}

int F17::rmi_f17_process_stick(struct rmi_f17_stick_data *stick) {
    int retval = 0;
    if (stick->query.general.has_absolute) {
        retval = rmiBus->readBlock(stick->data.abs.address,
            stick->data.abs.regs, sizeof(stick->data.abs.regs));
        if (retval < 0) {
            IOLogError("%s: Failed to read abs data for stick %d, code %d", __func__, stick->index, retval);
        } else {
            IOLogDebug("%s: Reporting x_force_high: %d, x_force_low: %d, y_force_high: %d, y_force_low: %d, z_force: %d\n",
                       __func__,
                       stick->data.abs.x_force_high,
                       stick->data.abs.x_force_low,
                       stick->data.abs.y_force_high,
                       stick->data.abs.y_force_low,
                       stick->data.abs.z_force);
        }
    }

    if (stick->query.general.has_relative && !retval) {
        retval = rmiBus->readBlock(stick->data.rel.address,
            stick->data.rel.regs, sizeof(stick->data.rel.regs));
        if (retval < 0) {
            IOLogError("%s: Failed to read rel data for stick %d, code %d", __func__, stick->index, retval);
        } else {
            IOLogDebug("%s: Reporting dX: %d, dy: %d\n", __func__, stick->data.rel.x_delta, stick->data.rel.y_delta);
            if (voodooTrackpointInstance && *voodooTrackpointInstance) {
                AbsoluteTime timestamp;
                clock_get_uptime(&timestamp);
                
                relativeEvent.dx = (SInt32)((SInt64)stick->data.rel.x_delta * conf->trackpointMult / DEFAULT_MULT);
                relativeEvent.dy = -(SInt32)((SInt64)stick->data.rel.y_delta * conf->trackpointMult / DEFAULT_MULT);
                relativeEvent.buttons = 0;
                relativeEvent.timestamp = timestamp;
                
                messageClient(kIOMessageVoodooTrackpointRelativePointer, *voodooTrackpointInstance, &relativeEvent, sizeof(RelativePointerEvent));
            }
        }
    }

    if (stick->query.general.has_gestures && !retval) {
        retval = rmiBus->readBlock(stick->data.gestures.address,
            stick->data.gestures.regs,
            sizeof(stick->data.gestures.regs));
        if (retval < 0) {
            IOLogError("%s: Failed to read gestures for stick %d, code %d", __func__,
                stick->index, retval);
        } else {
            IOLogDebug("%s: Reporting gesture: %d\n", __func__, stick->data.gestures.regs[0]);
        }
    }

    return retval;
}
