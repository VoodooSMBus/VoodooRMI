/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F11.c
 *
 * Copyright (c) 2011-2015 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F11.hpp"

OSDefineMetaClassAndStructors(F11, RMIFunction)
#define super IOService

#define REZERO_WAIT_MS 100

bool F11::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;
    
    sensor = OSTypeAlloc(RMI2DSensor);
    if (!sensor || !sensor->init())
        return false;

    return true;
}

bool F11::attach(IOService *provider)
{
    int error;


    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F11: Provider is not RMIBus");
        return false;
    }

    sensor->conf = conf;
    sensor->voodooInputInstance = rmiBus->getVoodooInput();
    
    error = rmi_f11_initialize();
    if (error)
        return false;
    
    super::attach(provider);
    
    return true;
}

bool F11::start(IOService *provider)
{
    if (!super::start(provider))
        return false;
    
    int rc = rmi_f11_config();
    
    if (rc < 0)
        return false;
    
    registerService();
    
    if(!sensor->attach(this))
        return false;
    
    if(!sensor->start(this))
        return false;
    
    return true;
}

void F11::stop(IOService *provider)
{
    sensor->detach(this);
    sensor->stop(this);
    super::stop(provider);
}

void F11::free()
{
    clearDesc();
    OSSafeReleaseNULL(sensor);
    super::free();
}

IOReturn F11::message(UInt32 type, IOService *provider, void *argument)
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
            rmi_f11_config();
            break;
    }
    
    return kIOReturnSuccess;
}

bool F11::getReport()
{
    int error, fingers, abs_size;
    u8 finger_state;
    AbsoluteTime timestamp;
    
    if (!sensor)
        return false;
    
    error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                              sensor->data_pkt, sensor->pkt_size);
    
    if (error < 0) {
        IOLogError("Could not read F11 attention data: %d", error);
        return false;
    }
    
    clock_get_uptime(&timestamp);
    
    if (sensor->shouldDiscardReport(timestamp))
        return true;
    
    IOLogDebug("F11 Packet");
    
    abs_size = sensor->nbr_fingers & RMI_F11_ABS_BYTES;
    
    if (abs_size > sensor->pkt_size)
        fingers = sensor->pkt_size / RMI_F11_ABS_BYTES;
    else fingers = sensor->nbr_fingers;
    
    for (int i = 0; i < fingers; i++) {
        finger_state = rmi_f11_parse_finger_state(i);
        u8 *pos_data = &data_2d.abs_pos[i * RMI_F11_ABS_BYTES];
        
        if (finger_state == F11_RESERVED) {
            IOLogError("Invalid finger state[%d]: 0x%02x",
                       i, finger_state);
            continue;
        }
        
        report.objs[i].x = (pos_data[0] << 4) | (pos_data[2] & 0x0F);
        report.objs[i].y = (pos_data[1] << 4) | (pos_data[2] >> 4);
        report.objs[i].z = pos_data[4];
        report.objs[i].wx = pos_data[3] & 0x0f;
        report.objs[i].wy = pos_data[3] >> 4;
        
        switch (finger_state) {
            case F11_PRESENT:
                report.objs[i].type = RMI_2D_OBJECT_FINGER;
                break;
            case F11_INACCURATE:
                report.objs[i].type = RMI_2D_OBJECT_INACCURATE;
                break;
            default:
                report.objs[i].type = RMI_2D_OBJECT_NONE;
        }
    }
    
    report.timestamp = timestamp;
    report.fingers = fingers;
    
    messageClient(kHandleRMIInputReport, sensor, &report, sizeof(RMI2DSensorReport));
    
    return true;
}

int F11::rmi_f11_config()
{
    return f11_write_control_regs(&sens_query, &dev_controls,
                                fn_descriptor->query_base_addr);
}

int F11::f11_read_control_regs(f11_2d_ctrl *ctrl, u16 ctrl_base_addr)
{
    int error = 0;
    
    ctrl->ctrl0_11_address = ctrl_base_addr;
    error = rmiBus->readBlock(ctrl_base_addr, ctrl->ctrl0_11,
                           RMI_F11_CTRL_REG_COUNT);
    if (error < 0) {
        IOLogError("Failed to read ctrl0, code: %d.", error);
        return error;
    }
    
    return 0;
}

int F11::f11_write_control_regs(f11_2d_sensor_queries *query,
                                f11_2d_ctrl *ctrl,
                                u16 ctrl_base_addr)
{
    int error;
    
    error = rmiBus->blockWrite(ctrl_base_addr, ctrl->ctrl0_11,
                            RMI_F11_CTRL_REG_COUNT);
    if (error < 0)
        return error;
    
    return 0;
}

int F11::f11_2d_construct_data()
{
    f11_2d_sensor_queries *query = &sens_query;
    f11_2d_data *data = &data_2d;
    int i;
    
    sensor->nbr_fingers = (query->nr_fingers == 5 ? 10 :
                           query->nr_fingers + 1);
    
    sensor->pkt_size = DIV_ROUND_UP(sensor->nbr_fingers, 4);
    
    if (query->has_abs) {
        sensor->pkt_size += (sensor->nbr_fingers * 5);
        sensor->attn_size = sensor->pkt_size;
    }
    
    if (query->has_rel)
        sensor->pkt_size +=  (sensor->nbr_fingers * 2);
    
    /* Check if F11_2D_Query7 is non-zero */
    if (query->query7_nonzero)
        sensor->pkt_size += sizeof(u8);
    
    /* Check if F11_2D_Query7 or F11_2D_Query8 is non-zero */
    if (query->query7_nonzero || query->query8_nonzero)
        sensor->pkt_size += sizeof(u8);
    
    if (query->has_pinch || query->has_flick || query->has_rotate) {
        sensor->pkt_size += 3;
        if (!query->has_flick)
            sensor->pkt_size--;
        if (!query->has_rotate)
            sensor->pkt_size--;
    }
    
    if (query->has_touch_shapes)
        sensor->pkt_size +=
            DIV_ROUND_UP(query->nr_touch_shapes + 1, 8);
    
    sensor->data_pkt = reinterpret_cast<u8*>(IOMalloc(sensor->pkt_size));
    
    if (!sensor->data_pkt)
        return -ENOMEM;
    
    data->f_state = sensor->data_pkt;
    i = DIV_ROUND_UP(sensor->nbr_fingers, 4);
    
    if (query->has_abs) {
        data->abs_pos = &sensor->data_pkt[i];
    }
    
    return 0;
}


// Tbh, we probably don't need most of this. This is more for troubleshooting/looking cool in IOReg
// I'm implementing because I'm curious...no other good reason
int F11::rmi_f11_get_query_parameters(f11_2d_sensor_queries *sensor_query,
                                 u16 query_base_addr)
{
    int query_size;
    int rc;
    u8 query_buf[RMI_F11_QUERY_SIZE];
    bool has_query36 = false;
    
    rc = rmiBus->readBlock(query_base_addr, query_buf,
                        RMI_F11_QUERY_SIZE);
    if (rc < 0)
        return rc;
    
    sensor_query->nr_fingers = query_buf[0] & RMI_F11_NR_FINGERS_MASK;
    sensor_query->has_rel = !!(query_buf[0] & RMI_F11_HAS_REL);
    sensor_query->has_abs = !!(query_buf[0] & RMI_F11_HAS_ABS);
    sensor_query->has_gestures = !!(query_buf[0] & RMI_F11_HAS_GESTURES);
    sensor_query->has_sensitivity_adjust =
        !!(query_buf[0] & RMI_F11_HAS_SENSITIVITY_ADJ);
    sensor_query->configurable = !!(query_buf[0] & RMI_F11_CONFIGURABLE);
    
    sensor_query->nr_x_electrodes =
        query_buf[1] & RMI_F11_NR_ELECTRODES_MASK;
    sensor_query->nr_y_electrodes =
        query_buf[2] & RMI_F11_NR_ELECTRODES_MASK;
    sensor_query->max_electrodes =
        query_buf[3] & RMI_F11_NR_ELECTRODES_MASK;
    
    setProperty("Number Fingers", sensor_query->nr_fingers, 8);
    setProperty("Has Relative", sensor_query->has_rel);
    setProperty("Has Absolute", sensor_query->has_abs);
    setProperty("Has Gestures", sensor_query->has_gestures);
    setProperty("Has Sensitivity Adjust", sensor_query->has_sensitivity_adjust);
    setProperty("Configurable", sensor_query->configurable);
    setProperty("Number of X Electrodes", sensor_query->nr_x_electrodes, 8);
    setProperty("Number of Y Electrodes", sensor_query->nr_y_electrodes, 8);
    setProperty("Max Number of Electrodes", sensor_query->max_electrodes, 8);
    
    query_size = RMI_F11_QUERY_SIZE;
    OSNumber *value;
    if (sensor_query->has_abs) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->abs_data_size =
            query_buf[0] & RMI_F11_ABS_DATA_SIZE_MASK;
        sensor_query->has_anchored_finger =
            !!(query_buf[0] & RMI_F11_HAS_ANCHORED_FINGER);
        sensor_query->has_adj_hyst =
            !!(query_buf[0] & RMI_F11_HAS_ADJ_HYST);
        sensor_query->has_dribble =
            !!(query_buf[0] & RMI_F11_HAS_DRIBBLE);
        sensor_query->has_bending_correction =
            !!(query_buf[0] & RMI_F11_HAS_BENDING_CORRECTION);
        sensor_query->has_large_object_suppression =
            !!(query_buf[0] & RMI_F11_HAS_LARGE_OBJECT_SUPPRESSION);
        sensor_query->has_jitter_filter =
            !!(query_buf[0] & RMI_F11_HAS_JITTER_FILTER);
        query_size++;
        
        OSDictionary *absProps = OSDictionary::withCapacity(7);
        setPropertyNumber(absProps, "Absolute Data Size", sensor_query->abs_data_size, 8);
        setPropertyBoolean(absProps, "Has Anchored Finger", sensor_query->has_anchored_finger);
        setPropertyBoolean(absProps, "Has Adjustable Hyst", sensor_query->has_adj_hyst);
        setPropertyBoolean(absProps, "Has Dribble", sensor_query->has_dribble);
        setPropertyBoolean(absProps, "Has Bending Correction", sensor_query->has_bending_correction);
        setPropertyBoolean(absProps, "Has Large Object Suppression", sensor_query->has_large_object_suppression);
        setPropertyBoolean(absProps, "Has Jitter Filter", sensor_query->has_jitter_filter);
        setProperty("Absolute Keys", absProps);
        absProps->release();
    }
    
    if (sensor_query->has_rel) {
        rc = rmiBus->read(query_base_addr + query_size,
                      &sensor_query->f11_2d_query6);
        if (rc < 0)
            return rc;
        query_size++;
    }
    
    if (sensor_query->has_gestures) {
        rc = rmiBus->readBlock(query_base_addr + query_size,
                            query_buf, RMI_F11_QUERY_GESTURE_SIZE);
        if (rc < 0)
            return rc;
        
        sensor_query->has_single_tap =
            !!(query_buf[0] & RMI_F11_HAS_SINGLE_TAP);
        sensor_query->has_tap_n_hold =
            !!(query_buf[0] & RMI_F11_HAS_TAP_AND_HOLD);
        sensor_query->has_double_tap =
            !!(query_buf[0] & RMI_F11_HAS_DOUBLE_TAP);
        sensor_query->has_early_tap =
            !!(query_buf[0] & RMI_F11_HAS_EARLY_TAP);
        sensor_query->has_flick =
            !!(query_buf[0] & RMI_F11_HAS_FLICK);
        sensor_query->has_press =
            !!(query_buf[0] & RMI_F11_HAS_PRESS);
        sensor_query->has_pinch =
            !!(query_buf[0] & RMI_F11_HAS_PINCH);
        sensor_query->has_chiral =
            !!(query_buf[0] & RMI_F11_HAS_CHIRAL);
        
        /* query 8 */
        sensor_query->has_palm_det =
            !!(query_buf[1] & RMI_F11_HAS_PALM_DET);
        sensor_query->has_rotate =
            !!(query_buf[1] & RMI_F11_HAS_ROTATE);
        sensor_query->has_touch_shapes =
            !!(query_buf[1] & RMI_F11_HAS_TOUCH_SHAPES);
        sensor_query->has_scroll_zones =
            !!(query_buf[1] & RMI_F11_HAS_SCROLL_ZONES);
        sensor_query->has_individual_scroll_zones =
            !!(query_buf[1] & RMI_F11_HAS_INDIVIDUAL_SCROLL_ZONES);
        sensor_query->has_mf_scroll =
            !!(query_buf[1] & RMI_F11_HAS_MF_SCROLL);
        sensor_query->has_mf_edge_motion =
            !!(query_buf[1] & RMI_F11_HAS_MF_EDGE_MOTION);
        sensor_query->has_mf_scroll_inertia =
            !!(query_buf[1] & RMI_F11_HAS_MF_SCROLL_INERTIA);
        
        sensor_query->query7_nonzero = !!(query_buf[0]);
        sensor_query->query8_nonzero = !!(query_buf[1]);
        
        OSDictionary *gestProps = OSDictionary::withCapacity(16);
        setPropertyBoolean(gestProps, "Has Single Tap", sensor_query->has_single_tap);
        setPropertyBoolean(gestProps, "Has Tap and Hold", sensor_query->has_tap_n_hold);
        setPropertyBoolean(gestProps, "Has Double Tap", sensor_query->has_double_tap);
        setPropertyBoolean(gestProps, "Has Early Tap", sensor_query->has_early_tap);
        setPropertyBoolean(gestProps, "Has Flick", sensor_query->has_flick);
        setPropertyBoolean(gestProps, "Has Press", sensor_query->has_press);
        setPropertyBoolean(gestProps, "Has Pinch", sensor_query->has_pinch);
        setPropertyBoolean(gestProps, "Has Chiral", sensor_query->has_chiral);

        setPropertyBoolean(gestProps, "Has Palm Detection", sensor_query->has_palm_det);
        setPropertyBoolean(gestProps, "Has Rotate", sensor_query->has_rotate);
        setPropertyBoolean(gestProps, "Has Touch Shapes", sensor_query->has_touch_shapes);
        setPropertyBoolean(gestProps, "Has Scroll Zones", sensor_query->has_scroll_zones);
        setPropertyBoolean(gestProps, "Has Individual Scroll Zones", sensor_query->has_individual_scroll_zones);
        setPropertyBoolean(gestProps, "Has Multi-Finger Scroll", sensor_query->has_mf_scroll);
        setPropertyBoolean(gestProps, "Has Multi-Finger Edge Motion", sensor_query->has_mf_edge_motion);
        setPropertyBoolean(gestProps, "Has Multi-Finger Scroll Intertia", sensor_query->has_mf_scroll_inertia);
        
        setProperty("Gestures", gestProps);
        gestProps->release();
        query_size += 2;
    }
    
    if (has_query9) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->has_pen =
            !!(query_buf[0] & RMI_F11_HAS_PEN);
        sensor_query->has_proximity =
            !!(query_buf[0] & RMI_F11_HAS_PROXIMITY);
        sensor_query->has_palm_det_sensitivity =
            !!(query_buf[0] & RMI_F11_HAS_PALM_DET_SENSITIVITY);
        sensor_query->has_suppress_on_palm_detect =
            !!(query_buf[0] & RMI_F11_HAS_SUPPRESS_ON_PALM_DETECT);
        sensor_query->has_two_pen_thresholds =
            !!(query_buf[0] & RMI_F11_HAS_TWO_PEN_THRESHOLDS);
        sensor_query->has_contact_geometry =
            !!(query_buf[0] & RMI_F11_HAS_CONTACT_GEOMETRY);
        sensor_query->has_pen_hover_discrimination =
            !!(query_buf[0] & RMI_F11_HAS_PEN_HOVER_DISCRIMINATION);
        sensor_query->has_pen_filters =
            !!(query_buf[0] & RMI_F11_HAS_PEN_FILTERS);
        
        OSDictionary *penProps = OSDictionary::withCapacity(8);
        setPropertyBoolean(penProps, "Has Pen", sensor_query->has_pen);
        setPropertyBoolean(penProps, "Has Proximity", sensor_query->has_proximity);
        setPropertyBoolean(penProps, "Has Palm Detection Sensitivity", sensor_query->has_palm_det_sensitivity);
        setPropertyBoolean(penProps, "Has Suppress on Palm Detect", sensor_query->has_suppress_on_palm_detect);
        setPropertyBoolean(penProps, "Has Two Pen Thresholds", sensor_query->has_two_pen_thresholds);
        setPropertyBoolean(penProps, "Has Contact Geometry", sensor_query->has_contact_geometry);
        setPropertyBoolean(penProps, "Has Pen Hover Discrimination", sensor_query->has_pen_hover_discrimination);
        setPropertyBoolean(penProps, "Has Pen Filters", sensor_query->has_pen_filters);
        setProperty("Pen", penProps);
        penProps->release();
        
        query_size++;
    }
    
    if (sensor_query->has_touch_shapes) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->nr_touch_shapes = query_buf[0] &
            RMI_F11_NR_TOUCH_SHAPES_MASK;
        
        setProperty("Number of Touch Shapes", sensor_query->nr_touch_shapes, 8);
        
        query_size++;
    }
    
    if (has_query11) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->has_z_tuning =
            !!(query_buf[0] & RMI_F11_HAS_Z_TUNING);
        sensor_query->has_algorithm_selection =
            !!(query_buf[0] & RMI_F11_HAS_ALGORITHM_SELECTION);
        sensor_query->has_w_tuning =
            !!(query_buf[0] & RMI_F11_HAS_W_TUNING);
        sensor_query->has_pitch_info =
            !!(query_buf[0] & RMI_F11_HAS_PITCH_INFO);
        sensor_query->has_finger_size =
            !!(query_buf[0] & RMI_F11_HAS_FINGER_SIZE);
        sensor_query->has_segmentation_aggressiveness =
            !!(query_buf[0] &
               RMI_F11_HAS_SEGMENTATION_AGGRESSIVENESS);
        sensor_query->has_XY_clip =
            !!(query_buf[0] & RMI_F11_HAS_XY_CLIP);
        sensor_query->has_drumming_filter =
            !!(query_buf[0] & RMI_F11_HAS_DRUMMING_FILTER);
        
        OSDictionary *tuningProps = OSDictionary::withCapacity(8);
        setPropertyBoolean(tuningProps, "Has Z Tuning", sensor_query->has_z_tuning);
        setPropertyBoolean(tuningProps, "Has Algorithm Selection", sensor_query->has_algorithm_selection);
        setPropertyBoolean(tuningProps, "Has Width Tuning", sensor_query->has_w_tuning);
        setPropertyBoolean(tuningProps, "Has Pitch Info", sensor_query->has_pitch_info);
        setPropertyBoolean(tuningProps, "Has Finger Size", sensor_query->has_finger_size);
        setPropertyBoolean(tuningProps, "Has Segmentation Agressiveness", sensor_query->has_segmentation_aggressiveness);
        setPropertyBoolean(tuningProps, "Has XY Clip", sensor_query->has_XY_clip);
        setPropertyBoolean(tuningProps, "Has Drumming Filter", sensor_query->has_drumming_filter);
        setProperty("Tuning (Query 11)", tuningProps);
        tuningProps->release();
        query_size++;
    }
    
    if (has_query12) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->has_gapless_finger =
            !!(query_buf[0] & RMI_F11_HAS_GAPLESS_FINGER);
        sensor_query->has_gapless_finger_tuning =
            !!(query_buf[0] & RMI_F11_HAS_GAPLESS_FINGER_TUNING);
        sensor_query->has_8bit_w =
            !!(query_buf[0] & RMI_F11_HAS_8BIT_W);
        sensor_query->has_adjustable_mapping =
            !!(query_buf[0] & RMI_F11_HAS_ADJUSTABLE_MAPPING);
        sensor_query->has_info2 =
            !!(query_buf[0] & RMI_F11_HAS_INFO2);
        sensor_query->has_physical_props =
            !!(query_buf[0] & RMI_F11_HAS_PHYSICAL_PROPS);
        sensor_query->has_finger_limit =
            !!(query_buf[0] & RMI_F11_HAS_FINGER_LIMIT);
        sensor_query->has_linear_coeff_2 =
            !!(query_buf[0] & RMI_F11_HAS_LINEAR_COEFF);
        
        OSDictionary *tuningProps2 = OSDictionary::withCapacity(8);
        setPropertyBoolean(tuningProps2, "Has Gapless Finger", sensor_query->has_gapless_finger);
        setPropertyBoolean(tuningProps2, "Has Gapless Finger Tuning", sensor_query->has_gapless_finger_tuning);
        setPropertyBoolean(tuningProps2, "Has 8 Bit Width", sensor_query->has_8bit_w);
        setPropertyBoolean(tuningProps2, "Has Adjustable Mapping", sensor_query->has_adjustable_mapping);
        setPropertyBoolean(tuningProps2, "Has Info2 (Query 14 present)", sensor_query->has_info2);
        setPropertyBoolean(tuningProps2, "Has Physical Properties", sensor_query->has_physical_props);
        setPropertyBoolean(tuningProps2, "Has Finger Limit", sensor_query->has_finger_limit);
        setPropertyBoolean(tuningProps2, "Has Linear Coefficient 2", sensor_query->has_linear_coeff_2);
        setProperty("Tuning (Query 12)", tuningProps2);
        tuningProps2->release();
        query_size++;
    }

    if (sensor_query->has_jitter_filter) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->jitter_window_size = query_buf[0] &
                                            RMI_F11_JITTER_WINDOW_MASK;
        sensor_query->jitter_filter_type = (query_buf[0] &
                                            RMI_F11_JITTER_FILTER_MASK) >>
                                            RMI_F11_JITTER_FILTER_SHIFT;
        
        OSDictionary *jitterProps = OSDictionary::withCapacity(2);
        setPropertyNumber(jitterProps, "Jitter Window Size", sensor_query->jitter_window_size, 8);
        setPropertyNumber(jitterProps, "Jitter Filter Type", sensor_query->jitter_filter_type, 8);
        setProperty("Jitter", jitterProps);
        jitterProps->release();
        query_size++;
    }
    
    if (sensor_query->has_info2) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->light_control =
            query_buf[0] & RMI_F11_LIGHT_CONTROL_MASK;
        sensor_query->is_clear =
            !!(query_buf[0] & RMI_F11_IS_CLEAR);
        sensor_query->clickpad_props =
            (query_buf[0] & RMI_F11_CLICKPAD_PROPS_MASK) >>
            RMI_F11_CLICKPAD_PROPS_SHIFT;
        sensor_query->mouse_buttons =
            (query_buf[0] & RMI_F11_MOUSE_BUTTONS_MASK) >>
            RMI_F11_MOUSE_BUTTONS_SHIFT;
        sensor_query->has_advanced_gestures =
            !!(query_buf[0] & RMI_F11_HAS_ADVANCED_GESTURES);
    
        OSDictionary *miscProps = OSDictionary::withCapacity(5);
        setPropertyNumber(miscProps, "Light Control", sensor_query->light_control, 8);
        setPropertyNumber(miscProps, "Clickpad Properties", sensor_query->clickpad_props, 8);
        setPropertyNumber(miscProps, "Mouse Buttons", sensor_query->mouse_buttons, 8);
        setPropertyBoolean(miscProps, "Is Clear", sensor_query->is_clear);
        setPropertyBoolean(miscProps, "Has Advanced Gestures", sensor_query->has_advanced_gestures);
        setProperty("Misc", miscProps);
        miscProps->release();
        query_size++;
    }
    
    if (sensor_query->has_physical_props) {
        rc = rmiBus->readBlock(query_base_addr
                            + query_size, query_buf, 4);
        if (rc < 0)
            return rc;
        
        sensor_query->x_sensor_size_mm =
            (query_buf[0] | (query_buf[1] << 8)) / 10;
        sensor_query->y_sensor_size_mm =
            (query_buf[2] | (query_buf[3] << 8)) / 10;
        
        OSDictionary *sizeProps = OSDictionary::withCapacity(2);
        setPropertyNumber(sizeProps, "X Sensor Size (mm)", sensor_query->x_sensor_size_mm, 16);
        setPropertyNumber(sizeProps, "Y Sensor Size (mm)", sensor_query->y_sensor_size_mm, 16);
        setProperty("Size", sizeProps);
        sizeProps->release();
        /*
         * query 15 - 18 contain the size of the sensor
         * and query 19 - 26 contain bezel dimensions
         */
        query_size += 12;
    }
    
    if (has_query27)
        ++query_size;
    
    /*
     * Are these two queries always done together?
     * +2 on query size suggests so, though sorta weird
     * they didn't just do +1 for each.
     * The check for has_query36 in here suggests not though
     */
    if (has_query28) {
        rc = rmiBus->read(query_base_addr + query_size,
                      query_buf);
        if (rc < 0)
            return rc;
        
        has_query36 = !!(query_buf[0] & BIT(6));
    }
    
    if (has_query36) {
        query_size += 2;
        rc = rmiBus->read(query_base_addr + query_size,
                          query_buf);
        if (rc < 0)
            return rc;
        
        if (!!(query_buf[0] & BIT(5)))
            has_acm = true;
    }
    
    return query_size;
}

int F11::rmi_f11_initialize()
{
    u8 query_offset, buf;
    u16 query_base_addr, control_base_addr;
    u16 max_x_pos, max_y_pos;
    int rc;
    
    // supposed to be default platform data - I can't find it though
    // Going to assume 100ms as in other places
    rezero_wait_ms = REZERO_WAIT_MS;
    
    query_base_addr = fn_descriptor->query_base_addr;
    control_base_addr = fn_descriptor->control_base_addr;
    
    rc = rmiBus->read(query_base_addr, &buf);
    if (rc < 0) {
        IOLogError("F11: Could not read Query Base Addr");
        return rc;
    }
    
    has_query9 = !!(buf & RMI_F11_HAS_QUERY9);
    has_query11 = !!(buf & RMI_F11_HAS_QUERY11);
    has_query12 = !!(buf & RMI_F11_HAS_QUERY12);
    has_query27 = !!(buf & RMI_F11_HAS_QUERY27);
    has_query28 = !!(buf & RMI_F11_HAS_QUERY28);
    
    query_offset = (query_base_addr + 1);
    
    rc = rmi_f11_get_query_parameters(&sens_query, query_offset);
    if (rc < 0) {
        IOLogError("F11: Could not read Sensor Query");
        return rc;
    }
    query_offset += rc;
    
    if (sens_query.has_physical_props) {
        sensor->x_mm = sens_query.x_sensor_size_mm;
        sensor->y_mm = sens_query.y_sensor_size_mm;
    } else {
        IOLogError("No size data from Device.");
        return -ENODEV;
    }
    
    if (!sens_query.has_abs) {
        IOLogError("No absolute reporting support!");
        return -ENODEV;
    }
    
    rc = rmiBus->readBlock(control_base_addr + F11_CTRL_SENSOR_MAX_X_POS_OFFSET,
                           (u8 *)&max_x_pos, sizeof(max_x_pos));
    if (rc < 0) {
        IOLogError("F11: Could not read max x");
        return rc;
    }
    
    rc = rmiBus->readBlock(control_base_addr + F11_CTRL_SENSOR_MAX_Y_POS_OFFSET,
                           (u8 *)&max_y_pos, sizeof(max_y_pos));
    if (rc < 0) {
        IOLogError("F11: Could not read max y");
        return rc;
    }
        
    sensor->max_x = max_x_pos;
    sensor->max_y = max_y_pos;
    
    rc = f11_2d_construct_data();
    if (rc < 0) {
        IOLogError("F11: Could not construct 2d Data");
        return rc;
    }
    
    if (has_acm)
        sensor->attn_size += sensor->nbr_fingers * 2;
    
    rc = f11_read_control_regs(&dev_controls,
                               control_base_addr);
    if (rc < 0) {
        IOLogError("Failed to read F11 control params.");
        return rc;
    }
    
    if (sens_query.has_dribble) {
        // RMI_REG_STATE_OFF
        dev_controls.ctrl0_11[0] &= ~BIT(6);
    }
    
    if (sens_query.has_palm_det) {
        // RMI_REG_STATE_OFF
        dev_controls.ctrl0_11[11] &= ~BIT(0);
    }
    
    rc = f11_write_control_regs(&sens_query,
                                &dev_controls, fn_descriptor->control_base_addr);
    if (rc)
        IOLogError("F11: Failed to write control registers");
    
    return 0;
}
