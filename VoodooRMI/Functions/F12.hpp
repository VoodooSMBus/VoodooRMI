/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F12.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#ifndef F12_hpp
#define F12_hpp

#include <RMIBus.hpp>

#define F12_DATA1_BYTES_PER_OBJ            8
#define RMI_REG_DESC_PRESENSE_BITS    (32 * BITS_PER_BYTE)
#define RMI_REG_DESC_SUBPACKET_BITS    (37 * BITS_PER_BYTE)

/* describes a single packet register */
struct rmi_register_desc_item {
    u16 reg;
    unsigned long reg_size;
    u8 num_subpackets;
    unsigned long subpacket_map[BITS_TO_LONGS(RMI_REG_DESC_SUBPACKET_BITS)];
};

/*
 * describes the packet registers for a particular type
 * (ie query, control, data)
 */
struct rmi_register_descriptor {
    unsigned long struct_size;
    unsigned long presense_map[BITS_TO_LONGS(RMI_REG_DESC_PRESENSE_BITS)];
    u8 num_registers;
    struct rmi_register_desc_item *registers;
};

enum rmi_f12_object_type {
    RMI_F12_OBJECT_NONE            = 0x00,
    RMI_F12_OBJECT_FINGER            = 0x01,
    RMI_F12_OBJECT_STYLUS            = 0x02,
    RMI_F12_OBJECT_PALM            = 0x03,
    RMI_F12_OBJECT_UNCLASSIFIED        = 0x04,
    RMI_F12_OBJECT_GLOVED_FINGER        = 0x06,
    RMI_F12_OBJECT_NARROW_OBJECT        = 0x07,
    RMI_F12_OBJECT_HAND_EDGE        = 0x08,
    RMI_F12_OBJECT_COVER            = 0x0A,
    RMI_F12_OBJECT_STYLUS_2            = 0x0B,
    RMI_F12_OBJECT_ERASER            = 0x0C,
    RMI_F12_OBJECT_SMALL_OBJECT        = 0x0D,
};

class F12 : public RMIFunction {
    OSDeclareDefaultStructors(F12)
    
public:
    bool init(OSDictionary *dictionary) override;
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    void free() override;
    
private:
    RMIBus *rmiBus;
    IOService *voodooInputInstance {nullptr};
    
    RMI2DSensorReport report {};
    
    static rmi_register_desc_item *rmi_get_register_desc_item(rmi_register_descriptor *rdesc, u16 reg);
    static size_t rmi_register_desc_calc_size(rmi_register_descriptor *rdesc);
    static int rmi_register_desc_calc_reg_offset(rmi_register_descriptor *rdesc, u16 reg);
    static bool rmi_register_desc_has_subpacket(const rmi_register_desc_item *item,
                                                u8 subpacket);
    
    int rmi_f12_config();
    
    /* F12 Data */
    RMI2DSensor *sensor;
    struct rmi_2d_sensor_platform_data sensor_pdata;
    bool has_dribble;
    
    rmi_register_descriptor query_reg_desc;
    rmi_register_descriptor control_reg_desc;
    rmi_register_descriptor data_reg_desc;
    
    /* F12 Data1 describes sensed objects */
    const rmi_register_desc_item *data1 {nullptr};
    u16 data1_offset;
    
    /* F12 Data5 describes finger ACM */
    const rmi_register_desc_item *data5 {nullptr};
    u16 data5_offset;
    
    /* F12 Data5 describes Pen */
    const rmi_register_desc_item *data6 {nullptr};
    u16 data6_offset;
    
    int rmi_f12_read_sensor_tuning();
    int rmi_read_register_desc(u16 addr,
                               rmi_register_descriptor *rdesc);
    
    void getReport();
    
};

#endif /* F12_hpp */
