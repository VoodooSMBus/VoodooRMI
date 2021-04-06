/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F12.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#include "F12.hpp"

OSDefineMetaClassAndStructors(F12, RMIFunction)
#define super IOService

bool F12::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;

    sensor = OSTypeAlloc(RMI2DSensor);
    if (!sensor || !sensor->init())
        return false;

    return true;
}

bool F12::attach(IOService *provider)
{
    int ret;
    u8 buf;
    u16 query_addr = fn_descriptor->query_base_addr;
    const rmi_register_desc_item *item;
    u16 data_offset = 0;

    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F12: Provider is not RMIBus");
        return false;
    }
    
    sensor->conf = conf;
    sensor->voodooInputInstance = rmiBus->getVoodooInput();
    
    ret = rmiBus->read(query_addr, &buf);
    if (ret < 0) {
        IOLogError("F12 - Failed to read general info register: %d", ret);
        return false;
    }
    ++query_addr;
    
    if (!(buf & BIT(0))) {
        IOLogError("F12 - Behaviour without register descriptors is undefined.");
        return false;
    }
    
    has_dribble = !!(buf & BIT(3));
    
    ret = rmi_read_register_desc(query_addr, &query_reg_desc);
    if (ret) {
        IOLogError ("F12 - Failed to read the Query Register Descriptor: %d", ret);
        return ret;
    }
    query_addr += 3;
    
    ret = rmi_read_register_desc(query_addr, &control_reg_desc);
    if (ret) {
        IOLogError("F12 - Failed to read the Control Register Descriptor: %d",
                   ret);
        return ret;
    }
    query_addr += 3;
    
    ret = rmi_read_register_desc(query_addr, &data_reg_desc);
    if (ret) {
        IOLogError("F12 - Failed to read the Data Register Descriptor: %d",
                   ret);
        return ret;
    }
    query_addr += 3;
    
    sensor->pkt_size = (int) rmi_register_desc_calc_size(&data_reg_desc);
    IOLogDebug("F12 - Data packet size: %d", sensor->pkt_size);
    
    sensor->data_pkt = reinterpret_cast<u8 *>(IOMalloc(sensor->pkt_size));
    
    if (!sensor->data_pkt)
        return -ENOMEM;
    
    ret = rmi_f12_read_sensor_tuning();
    if (ret) {
        IOLogError("F12 - Failed sensor tuning");
        return false;
    }
    
    /*
     * Figure out what data is contained in the data registers. HID devices
     * may have registers defined, but their data is not reported in the
     * HID attention report. Registers which are not reported in the HID
     * attention report check to see if the device is receiving data from
     * HID attention reports.
     */
    item = rmi_get_register_desc_item(&data_reg_desc, 0);
    if (item)
        data_offset += item->reg_size;
    
    item = rmi_get_register_desc_item(&data_reg_desc, 1);
    if (!item) {
        return false;
        IOLogError("F12 - No Data1 Reg!");
    }
    
    data1 = item;
    data1_offset = data_offset;
    data_offset += item->reg_size;
    sensor->nbr_fingers = item->num_subpackets;
    sensor->report_abs = 1;
    sensor->attn_size += item->reg_size;
    
    item = rmi_get_register_desc_item(&data_reg_desc, 2);
    if (item)
        data_offset += item->reg_size;
    
    item = rmi_get_register_desc_item(&data_reg_desc, 3);
    if (item)
        data_offset += item->reg_size;
    
    item = rmi_get_register_desc_item(&data_reg_desc, 4);
    if (item)
        data_offset += item->reg_size;
    
    item = rmi_get_register_desc_item(&data_reg_desc, 5);
    if (item) {
        data5 = item;
        data5_offset = data_offset;
        data_offset += item->reg_size;
        sensor->attn_size += item->reg_size;
    }
    
    // Skip 6-15 as they do not increase attention size and only gives relative info
    
    setProperty("Number of fingers", sensor->nbr_fingers, 8);
    IOLogDebug("F12 - Number of fingers %u", sensor->nbr_fingers);
    
    
    return super::attach(provider);
}

bool F12::start(IOService *provider)
{
    int ret = rmi_f12_config();
    if (ret < 0)
        return false;
    
    registerService();
    
    if (!sensor->attach(this))
        return false;
    
    if (!sensor->start(this))
        return false;
    
    return super::start(provider);
}

void F12::stop(IOService *provider)
{
    sensor->detach(this);
    sensor->stop(this);
    super::stop(provider);
}

void F12::free()
{
    clearDesc();
    OSSafeReleaseNULL(sensor);
    super::free();
}

IOReturn F12::message(UInt32 type, IOService *provider, void *argument)
{
    switch (type)
    {
        case kHandleRMIAttention:
            getReport();
            break;
        case kHandleRMIClickpadSet:
        case kHandleRMITrackpoint:
            return messageClient(type, sensor, argument);
        case kHandleRMIConfig:
            return rmi_f12_config();
    }
    
    return kIOReturnSuccess;
}

int F12::rmi_f12_config()
{
    const struct rmi_register_desc_item *item;
    unsigned long control_size;
    char buf[3];
    u8 subpacket_offset = 0;
    
    if (has_dribble) {
        item = rmi_get_register_desc_item(&control_reg_desc, 20);
        if (item) {
            u16 control_offset = rmi_register_desc_calc_reg_offset(&control_reg_desc, 20);
            
            /*
             * The byte containing the EnableDribble bit will be
             * in either byte 0 or byte 2 of control 20. Depending
             * on the existence of subpacket 0. If control 20 is
             * larger then 3 bytes, just read the first 3.
             */
            control_size = min(item->reg_size, 3UL);
            
            int ret = rmiBus->readBlock(fn_descriptor->control_base_addr
                                    + control_offset, (u8 *) buf, control_size);
            if (ret)
                return ret;
            
            if (rmi_register_desc_has_subpacket(item, 0))
                subpacket_offset += 1;
            
            switch (RMI_REG_STATE_OFF) {
                case RMI_REG_STATE_OFF:
                    buf[subpacket_offset] &= ~BIT(2);
                    break;
                case RMI_REG_STATE_ON:
                    buf[subpacket_offset] |= BIT(2);
                    break;
                case RMI_REG_STATE_DEFAULT:
                default:
                    break;
            }
            
            ret = rmiBus->blockWrite(fn_descriptor->control_base_addr + control_offset,
                                     (u8 *) buf, control_size);
            if (ret)
                return ret;
        }
    }
    
    return kIOReturnSuccess;
}

int F12::rmi_f12_read_sensor_tuning()
{
    const rmi_register_desc_item *item;
    int ret;
    int offset;
    u8 buf[15];
    int pitch_x = 0;
    int pitch_y = 0;
    int rx_receivers = 0;
    int tx_receivers = 0;
    
    item = rmi_get_register_desc_item(&control_reg_desc, 8);
    if (!item) {
        IOLogError("F12 - No sensor tuning Control register");
        return -ENODEV;
    }
    
    offset = rmi_register_desc_calc_reg_offset(&control_reg_desc, 8);
    
    if (item->reg_size > sizeof(buf)) {
        IOLogError("F12 - Control8 should be no bigger than %zd bytes, not: %ld",
                   sizeof(buf), item->reg_size);
        return -ENODEV;
    }
    
    ret = rmiBus->readBlock(fn_descriptor->control_base_addr + offset, buf,
                            item->reg_size);
    if (ret)
        return ret;
    
    offset = 0;
    if (rmi_register_desc_has_subpacket(item, 0)) {
        sensor->max_x = (buf[offset + 1] << 8) | buf[offset];
        sensor->max_y = (buf[offset + 3] << 8) | buf[offset + 2];
        offset += 4;
    } else {
        IOLogError("F12 - No size register");
        return -EIO;
    }
    
    if (rmi_register_desc_has_subpacket(item, 1)) {
        pitch_x = (buf[offset + 1] << 8) | buf[offset];
        pitch_y = (buf[offset + 3] << 8) | buf[offset + 2];
        offset += 4;
    } else {
        IOLogError("F12 - No pitch register");
        return -EIO;
    }
    
    if (rmi_register_desc_has_subpacket(item, 2)) {
        /* Units 1/128 sensor pitch */
        setProperty("Inactive Border (X Low)", buf[offset], 8);
        setProperty("Inactive Border (X High)", buf[offset + 1], 8);
        setProperty("Inactive Border (Y Low)", buf[offset + 2], 8);
        setProperty("Inactive Border (Y High)", buf[offset + 3], 8);
        
        offset += 4;
    }
    
    if (rmi_register_desc_has_subpacket(item, 3)) {
        rx_receivers = buf[offset];
        tx_receivers = buf[offset + 1];
        offset += 2;
    } else {
        IOLogError("No rx/tx receiver register");
        return -EIO;
    }
    
    /* Skip over sensor flags */
    if (rmi_register_desc_has_subpacket(item, 4))
        offset += 1;
    
    sensor->x_mm = (pitch_x * rx_receivers) >> 12;
    sensor->y_mm = (pitch_y * tx_receivers) >> 12;
    
    return 0;
}

void F12::getReport()
{
    AbsoluteTime timestamp;
    
    if (!sensor || !data1)
        return;
    
    int retval = rmiBus->readBlock(fn_descriptor->data_base_addr, sensor->data_pkt,
                                   sensor->pkt_size);
    
    if (retval < 0) {
        IOLogError("F12 - Failed to read object data. Code: %d", retval);
        return;
    }
    
    clock_get_uptime(&timestamp);
    if (sensor->shouldDiscardReport(timestamp))
        return;
    
    IOLogDebug("F12 Packet");
#if DEBUG
    if (sensor->nbr_fingers > 5) {
        IOLogDebug("More than 5 fingers!");
    }
#endif // debug
    
    int fingers = min (sensor->nbr_fingers, 5);
    u8 *data = &sensor->data_pkt[data1_offset];
    
    for (int i = 0; i < fingers; i++) {
        rmi_2d_sensor_abs_object *obj = &report.objs[i];
        
        switch (data[0]) {
            case RMI_F12_OBJECT_FINGER:
                obj->type = RMI_2D_OBJECT_FINGER;
                break;
            case RMI_F12_OBJECT_STYLUS:
                obj->type = RMI_2D_OBJECT_STYLUS;
                break;
            default:
                obj->type = RMI_2D_OBJECT_NONE;
        }
        
        obj->x = (data[2] << 8) | data[1];
        obj->y = (data[4] << 8) | data[3];
        obj->z = data[5];
        obj->wx = data[6];
        obj->wy = data[7];
        
        data += F12_DATA1_BYTES_PER_OBJ;
    }
    
    report.timestamp = timestamp;
    report.fingers = fingers;
    
    messageClient(kHandleRMIInputReport, sensor, &report, sizeof(RMI2DSensorReport));
}

int F12::rmi_read_register_desc(u16 addr,
                                rmi_register_descriptor *rdesc)
{
    int ret;
    u8 size_presence_reg;
    u8 buf[35];
    int presense_offset = 1;
    u8 *struct_buf;
    int reg;
    int offset = 0;
    int map_offset = 0;
    int i;
    int b;
    
    /*
     * The first register of the register descriptor is the size of
     * the register descriptor's presense register.
     */
    ret = rmiBus->read(addr, &size_presence_reg);
    if (ret)
        return ret;
    ++addr;
    
    if (size_presence_reg < 0 || size_presence_reg > 35)
        return -EIO;
    
    memset(buf, 0, sizeof(buf));
    
    /*
     * The presence register contains the size of the register structure
     * and a bitmap which identified which packet registers are present
     * for this particular register type (ie query, control, or data).
     */
    ret = rmiBus->readBlock(addr, buf, size_presence_reg);
    if (ret)
        return ret;
    ++addr;
    
    if (buf[0] == 0) {
        presense_offset = 3;
        rdesc->struct_size = buf[1] | (buf[2] << 8);
    } else {
        rdesc->struct_size = buf[0];
    }
    
    for (i = presense_offset; i < size_presence_reg; i++) {
        for (b = 0; b < 8; b++) {
            if (buf[i] & (0x1 << b))
                bitmap_set(rdesc->presense_map, map_offset, 1);
            ++map_offset;
        }
    }
    
    rdesc->num_registers = bitmap_weight(rdesc->presense_map,
                                         RMI_REG_DESC_PRESENSE_BITS);
    
    rdesc->registers = reinterpret_cast<rmi_register_desc_item *>(IOMalloc(rdesc->num_registers * sizeof(rmi_register_desc_item)));
    if (!rdesc->registers)
        return -ENOMEM;
    
    memset (rdesc->registers, 0, rdesc->num_registers * sizeof(rmi_register_desc_item));
    
    /*
     * Allocate a temporary buffer to hold the register structure.
     * I'm not using devm_kzalloc here since it will not be retained
     * after exiting this function
     */
    struct_buf = reinterpret_cast<u8 *>(IOMalloc(rdesc->struct_size));
    if (!struct_buf)
        return -ENOMEM;
    
    /*
     * The register structure contains information about every packet
     * register of this type. This includes the size of the packet
     * register and a bitmap of all subpackets contained in the packet
     * register.
     */
    ret = rmiBus->readBlock(addr, struct_buf, rdesc->struct_size);
    if (ret)
        goto free_struct_buff;
    
    reg = find_first_bit(rdesc->presense_map, RMI_REG_DESC_PRESENSE_BITS);
    for (i = 0; i < rdesc->num_registers; i++) {
        struct rmi_register_desc_item *item = &rdesc->registers[i];
        int reg_size = struct_buf[offset];
        
        ++offset;
        if (reg_size == 0) {
            reg_size = struct_buf[offset] |
                       (struct_buf[offset + 1] << 8);
            offset += 2;
        }
        
        if (reg_size == 0) {
            reg_size = struct_buf[offset] |
                       (struct_buf[offset + 1] << 8) |
                       (struct_buf[offset + 2] << 16) |
                       (struct_buf[offset + 3] << 24);
            offset += 4;
        }
        
        item->reg = reg;
        item->reg_size = reg_size;
        
        map_offset = 0;
        
        do {
            for (b = 0; b < 7; b++) {
                if (struct_buf[offset] & (0x1 << b))
                    bitmap_set(item->subpacket_map,
                               map_offset, 1);
                ++map_offset;
            }
        } while (struct_buf[offset++] & 0x80);
        
        item->num_subpackets = bitmap_weight(item->subpacket_map,
                                             RMI_REG_DESC_SUBPACKET_BITS);
        
        IOLogDebug("F12 - reg: %d reg size: %ld subpackets: %d",
                item->reg, item->reg_size, item->num_subpackets);
        
        reg = find_next_bit(rdesc->presense_map,
                            RMI_REG_DESC_PRESENSE_BITS, reg + 1);
    }
    
free_struct_buff:
    IOFree(struct_buf, rdesc->struct_size);
    return ret;
}

/* Compute the register offset relative to the base address */
int F12::rmi_register_desc_calc_reg_offset(rmi_register_descriptor *rdesc, u16 reg)
{
    const struct rmi_register_desc_item *item;
    int offset = 0;
    int i;
    
    for (i = 0; i < rdesc->num_registers; i++) {
        item = &rdesc->registers[i];
        if (item->reg == reg)
            return offset;
        ++offset;
    }
    return -1;
}

size_t F12::rmi_register_desc_calc_size(rmi_register_descriptor *rdesc)
{
    const rmi_register_desc_item *item;
    int i;
    size_t size = 0;
    
    for (i = 0; i < rdesc->num_registers; i++) {
        item = &rdesc->registers[i];
        size += item->reg_size;
    }
    return size;
}

rmi_register_desc_item *F12::rmi_get_register_desc_item(rmi_register_descriptor *rdesc, u16 reg)
{
    rmi_register_desc_item *item;
    int i;
    
    for (i = 0; i < rdesc->num_registers; i++) {
        item = &rdesc->registers[i];
        if (item->reg == reg)
            return item;
    }
    
    return NULL;
}

bool F12::rmi_register_desc_has_subpacket(const rmi_register_desc_item *item,
                                          u8 subpacket)
{
    return find_next_bit(item->subpacket_map, RMI_REG_DESC_PRESENSE_BITS,
                         subpacket) == subpacket;
}
