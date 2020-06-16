/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F11.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F11.hpp"

OSDefineMetaClassAndStructors(F11, RMIFunction)
#define super IOService

#define REZERO_WAIT_MS 100
#define MilliToNano 1000000

// macOS kernel/math has absolute value in it. It's only for doubles though
#define abs(x) ((x < 0) ? (-x) : (x))

bool F11::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;
    
    dev_controls_mutex = IOLockAlloc();
    disableWhileTypingTimeout =
        Configuration::loadUInt64Configuration(dictionary, "DisableWhileTypingTimeout", 500) * MilliToNano;
    forceTouchMinPressure =
        Configuration::loadUInt32Configuration(dictionary, "ForceTouchMinPressure", 80);
    forceTouchEmulation = Configuration::loadBoolConfiguration(dictionary, "ForceTouchEmulation", true);
    
    return dev_controls_mutex;
}

bool F11::attach(IOService *provider)
{
    int error;
    
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F11: Provider is not RMIBus\n");
        return false;
    }
    
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
    
    int rc;
    
    rc = f11_write_control_regs(&sens_query,
                                &dev_controls, fn_descriptor->query_base_addr);
    
    if (rc < 0)
        return !rc;
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, sensor.max_x, 16);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, sensor.max_y, 16);
    // Need to be in 0.01mm units
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, sens_query.x_sensor_size_mm * 100, 16);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, sens_query.y_sensor_size_mm * 100, 16);
    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    // VoodooPS2 keyboard notifs
    setProperty("RM,deliverNotifications", kOSBooleanTrue);
    
    memset(freeFingerType, true, kMT2FingerTypeCount);
    freeFingerType[kMT2FingerTypeUndefined] = false;
    
    registerService();
    return true;
}

void F11::stop(IOService *provider)
{
    super::stop(provider);
}

void F11::free()
{
    clearDesc();
    IOLockFree(dev_controls_mutex);
//    if (sensor.data_pkt)
//        IOFree(sensor.data_pkt, sizeof(sensor.pkt_size));
    
    super::free();
}

bool F11::handleOpen(IOService *forClient, IOOptionBits options, void *arg)
{
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    
    return super::handleOpen(forClient, options, arg);
}

void F11::handleClose(IOService *forClient, IOOptionBits options)
{
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
}

IOReturn F11::message(UInt32 type, IOService *provider, void *argument)
{
    switch (type)
    {
        case kHandleRMIAttention:
            getReport();
            break;
        case kHandleRMIClickpadSet:
            clickpadState = !!(argument);
            break;
        case kHandleRMITrackpoint:
            // Re-use keyboard var as it's the same thin
            uint64_t timestamp;
            clock_get_uptime(&timestamp);
            absolutetime_to_nanoseconds(timestamp, &lastKeyboardTS);
            break;
        
        // VoodooPS2 Messages
        case kKeyboardKeyPressTime:
            lastKeyboardTS = *((uint64_t*) argument);
            break;
        case kKeyboardGetTouchStatus: {
            bool *result = (bool *) argument;
            *result = touchpadEnable;
            break;
        }
        case kKeyboardSetTouchStatus:
            touchpadEnable = *((bool *) argument);
            break;
    }
    
    return kIOReturnSuccess;
}

bool F11::getReport()
{
    int error, fingers, abs_size, realFingerCount = 0;
    u8 finger_state;
    AbsoluteTime timestamp;
    uint64_t timestampNS;
    
    error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                              sensor.data_pkt, sensor.pkt_size);
    
    if (error < 0)
    {
        IOLogError("Could not read F11 attention data: %d", error);
        return false;
    }
    
    clock_get_uptime(&timestamp);
    absolutetime_to_nanoseconds(timestamp, &timestampNS);
    
    if (!touchpadEnable || timestampNS - lastKeyboardTS < disableWhileTypingTimeout)
        return 0;
    
    abs_size = sensor.nbr_fingers & RMI_F11_ABS_BYTES;
    if (abs_size > sensor.pkt_size)
        fingers = sensor.pkt_size / RMI_F11_ABS_BYTES;
    else fingers = sensor.nbr_fingers;
    
    int transducer_count = 0;
    IOLogDebug("F11 Packet");
    for (int i = 0; i < fingers; i++) {
        finger_state = rmi_f11_parse_finger_state(i);
        if (finger_state == F11_RESERVED) {
            IOLogError("Invalid finger state[%d]: 0x%02x", i,
                       finger_state);
            continue;
        }
        
        auto& transducer = inputEvent.transducers[transducer_count++];
        transducer.type = FINGER;
        transducer.isValid = finger_state == F11_PRESENT;
        transducer.supportsPressure = true;
        
        if (finger_state == F11_PRESENT) {
            realFingerCount++;
            u8 *pos_data = &data_2d.abs_pos[i * RMI_F11_ABS_BYTES];
            u16 pos_x, pos_y;
            u8 wx, wy, z;
            
            pos_x = (pos_data[0] << 4) | (pos_data[2] & 0x0F);
            pos_y = (pos_data[1] << 4) | (pos_data[2] >> 4);
            z = pos_data[4];
            wx = pos_data[3] & 0x0f;
            wy = pos_data[3] >> 4;
            
            transducer.previousCoordinates = transducer.currentCoordinates;
            
            // Rudimentry palm detection
            transducer.isValid = z < 120 && abs(wx - wy) < 3;
            
            transducer.currentCoordinates.width = z / 1.5;
            
            if (!pressureLock) {
                transducer.currentCoordinates.x = pos_x;
                transducer.currentCoordinates.y = sensor.max_y - pos_y;
            } else {
                // Lock position for force touch
                transducer.currentCoordinates = transducer.previousCoordinates;
            }
                
            transducer.timestamp = timestamp;
            
            if (clickpadState && forceTouchEmulation && z > forceTouchMinPressure)
                pressureLock = true;
            
            transducer.currentCoordinates.pressure = pressureLock ? 255 : 0;
            transducer.isPhysicalButtonDown = clickpadState && !pressureLock;
            
            IOLogDebug("Finger num: %d (%d, %d) [Z: %u WX: %u WY: %u FingerType: %d Pressure : %d Button: %d]",
                       i, pos_x, pos_y, z, wx, wy, transducer.fingerType,
                       transducer.currentCoordinates.pressure,
                       transducer.isPhysicalButtonDown);
        }
        
        transducer.isTransducerActive = 1;
        transducer.secondaryId = i;
    }
    
    if (realFingerCount == 4 && freeFingerType[kMT2FingerTypeThumb]) {
        setThumbFingerType(fingers);
    }
    
    // Sencond loop to get type
    for (int i = 0; i < fingers; i++) {
        auto& trans = inputEvent.transducers[i];
        if (trans.isValid) {
            if (trans.fingerType == kMT2FingerTypeUndefined)
                trans.fingerType = getFingerType(&trans);
        } else {
            if (trans.fingerType != kMT2FingerTypeUndefined)
                freeFingerType[trans.fingerType] = true;
            trans.fingerType = kMT2FingerTypeUndefined;
        }
    }
    
    inputEvent.contact_count = transducer_count;
    inputEvent.timestamp = timestamp;
    
    if (!realFingerCount) {
        pressureLock = false;
    }
    
    messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    
    
    return true;
}

int F11::f11_read_control_regs(f11_2d_ctrl *ctrl, u16 ctrl_base_addr)
{
    int error = 0;
    
    ctrl->ctrl0_11_address = ctrl_base_addr;
    error = rmiBus->readBlock(ctrl_base_addr, ctrl->ctrl0_11,
                           RMI_F11_CTRL_REG_COUNT);
    if (error < 0) {
        IOLogError("Failed to read ctrl0, code: %d.\n", error);
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
    
    sensor.nbr_fingers = (query->nr_fingers == 5 ? 10 :
                           query->nr_fingers + 1);
    
    sensor.pkt_size = DIV_ROUND_UP(sensor.nbr_fingers, 4);
    
    if (query->has_abs) {
        sensor.pkt_size += (sensor.nbr_fingers * 5);
        sensor.attn_size = sensor.pkt_size;
    }
    
    if (query->has_rel)
        sensor.pkt_size +=  (sensor.nbr_fingers * 2);
    
    /* Check if F11_2D_Query7 is non-zero */
    if (query->query7_nonzero)
        sensor.pkt_size += sizeof(u8);
    
    /* Check if F11_2D_Query7 or F11_2D_Query8 is non-zero */
    if (query->query7_nonzero || query->query8_nonzero)
        sensor.pkt_size += sizeof(u8);
    
    if (query->has_pinch || query->has_flick || query->has_rotate) {
        sensor.pkt_size += 3;
        if (!query->has_flick)
            sensor.pkt_size--;
        if (!query->has_rotate)
            sensor.pkt_size--;
    }
    
    if (query->has_touch_shapes)
        sensor.pkt_size +=
            DIV_ROUND_UP(query->nr_touch_shapes + 1, 8);
    
    sensor.data_pkt = reinterpret_cast<u8*>(IOMalloc(sensor.pkt_size));
    
    if (!sensor.data_pkt)
        return -ENOMEM;
    
    data->f_state = sensor.data_pkt;
    i = DIV_ROUND_UP(sensor.nbr_fingers, 4);
    
    if (query->has_abs) {
        data->abs_pos = &sensor.data_pkt[i];
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
    
    setProperty("Number Fingers", OSNumber::withNumber(sensor_query->nr_fingers, 8));
    setProperty("Has Relative", OSBoolean::withBoolean(sensor_query->has_rel));
    setProperty("Has Absolute", OSBoolean::withBoolean(sensor_query->has_abs));
    setProperty("Has Gestures", OSBoolean::withBoolean(sensor_query->has_gestures));
    setProperty("Has Sensitivity Adjust", OSBoolean::withBoolean(sensor_query->has_sensitivity_adjust));
    setProperty("Configurable", OSBoolean::withBoolean(sensor_query->configurable));
    setProperty("Number of X Electrodes", OSNumber::withNumber(sensor_query->nr_x_electrodes, 8));
    setProperty("Number of Y Electrodes", OSNumber::withNumber(sensor_query->nr_y_electrodes, 8));
    setProperty("Max Number of Electrodes", OSNumber::withNumber(sensor_query->max_electrodes, 8));
    
    query_size = RMI_F11_QUERY_SIZE;
    
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
        
        absProps = OSDictionary::withCapacity(7);
        
        absProps->setObject("Absolute Data Size", OSNumber::withNumber(sensor_query->abs_data_size, 8));
        absProps->setObject("Has Anchored Finger", OSBoolean::withBoolean(sensor_query->has_anchored_finger));
        absProps->setObject("Has Adjustable Hyst", OSBoolean::withBoolean(sensor_query->has_adj_hyst));
        absProps->setObject("Has Dribble", OSBoolean::withBoolean(sensor_query->has_dribble));
        absProps->setObject("Has Bending Correction", OSBoolean::withBoolean(sensor_query->has_bending_correction));
        absProps->setObject("Has Large Object Suppression", OSBoolean::withBoolean(sensor_query->has_large_object_suppression));
        absProps->setObject("Has Jitter Filter", OSBoolean::withBoolean(sensor_query->has_jitter_filter));
        
        setProperty("Absolute Keys", absProps);
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
        
        gestProps = OSDictionary::withCapacity(16);
        gestProps->setObject("Has Single Tap", OSBoolean::withBoolean(sensor_query->has_single_tap));
        gestProps->setObject("Has Tap and Hold", OSBoolean::withBoolean(sensor_query->has_tap_n_hold));
        gestProps->setObject("Has Double Tap", OSBoolean::withBoolean(sensor_query->has_double_tap));
        gestProps->setObject("Has Early Tap", OSBoolean::withBoolean(sensor_query->has_early_tap));
        gestProps->setObject("Has Flick", OSBoolean::withBoolean(sensor_query->has_flick));
        gestProps->setObject("Has Press", OSBoolean::withBoolean(sensor_query->has_press));
        gestProps->setObject("Has Pinch", OSBoolean::withBoolean(sensor_query->has_pinch));
        gestProps->setObject("Has Chiral", OSBoolean::withBoolean(sensor_query->has_chiral));
        
        gestProps->setObject("Has Palm Detection", OSBoolean::withBoolean(sensor_query->has_palm_det));
        gestProps->setObject("Has Rotate", OSBoolean::withBoolean(sensor_query->has_rotate));
        gestProps->setObject("Has Touch Shapes", OSBoolean::withBoolean(sensor_query->has_touch_shapes));
        gestProps->setObject("Has Scroll Zones", OSBoolean::withBoolean(sensor_query->has_scroll_zones));
        gestProps->setObject("Has Individual Scroll Zones", OSBoolean::withBoolean(sensor_query->has_individual_scroll_zones));
        gestProps->setObject("Has Multi-Finger Scroll", OSBoolean::withBoolean(sensor_query->has_mf_scroll));
        gestProps->setObject("Has Multi-Finger Edge Motion", OSBoolean::withBoolean(sensor_query->has_mf_edge_motion));
        gestProps->setObject("Has Multi-Finger Scroll Intertia", OSBoolean::withBoolean(sensor_query->has_mf_scroll_inertia));
        
        setProperty("Gestures", gestProps);
        
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
        
        penProps = OSDictionary::withCapacity(8);
        penProps->setObject("Has Pen", OSBoolean::withBoolean(sensor_query->has_pen));
        penProps->setObject("Has Proximity", OSBoolean::withBoolean(sensor_query->has_proximity));
        penProps->setObject("Has Palm Detection Sensitivity", OSBoolean::withBoolean(sensor_query->has_palm_det_sensitivity));
        penProps->setObject("Has Suppress on Palm Detect", OSBoolean::withBoolean(sensor_query->has_suppress_on_palm_detect));
        penProps->setObject("Has Two Pen Thresholds", OSBoolean::withBoolean(sensor_query->has_two_pen_thresholds));
        penProps->setObject("Has Contact Geometry", OSBoolean::withBoolean(sensor_query->has_contact_geometry));
        penProps->setObject("Has Pen Hover Discrimination", OSBoolean::withBoolean(sensor_query->has_pen_hover_discrimination));
        penProps->setObject("Has Pen Filters", OSBoolean::withBoolean(sensor_query->has_pen_filters));
        
        setProperty("Pen", penProps);
        
        query_size++;
    }
    
    if (sensor_query->has_touch_shapes) {
        rc = rmiBus->read(query_base_addr + query_size, query_buf);
        if (rc < 0)
            return rc;
        
        sensor_query->nr_touch_shapes = query_buf[0] &
            RMI_F11_NR_TOUCH_SHAPES_MASK;
        
        setProperty("Number of Touch Shapes", OSNumber::withNumber(sensor_query->nr_touch_shapes, 8));
        
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
        
        tuningProps = OSDictionary::withCapacity(8);
        tuningProps->setObject("Has Z Tuning", OSBoolean::withBoolean(sensor_query->has_z_tuning));
        tuningProps->setObject("Has Algorithm Selection", OSBoolean::withBoolean(sensor_query->has_algorithm_selection));
        tuningProps->setObject("Has Width Tuning", OSBoolean::withBoolean(sensor_query->has_w_tuning));
        tuningProps->setObject("Has Pitch Info", OSBoolean::withBoolean(sensor_query->has_pitch_info));
        tuningProps->setObject("Has Finger Size", OSBoolean::withBoolean(sensor_query->has_finger_size));
        tuningProps->setObject("Has Segmentation Agressiveness", OSBoolean::withBoolean(sensor_query->has_segmentation_aggressiveness));
        tuningProps->setObject("Has XY Clip", OSBoolean::withBoolean(sensor_query->has_XY_clip));
        tuningProps->setObject("Has Drumming Filter", OSBoolean::withBoolean(sensor_query->has_drumming_filter));
        setProperty("Tuning (Query 11)", tuningProps);
        
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
        
        tuningProps2 = OSDictionary::withCapacity(8);
        tuningProps2->setObject("Has Gapless Finger", OSBoolean::withBoolean(sensor_query->has_gapless_finger));
        tuningProps2->setObject("Has Gapless Finger Tuning", OSBoolean::withBoolean(sensor_query->has_gapless_finger_tuning));
        tuningProps2->setObject("Has 8 Bit Width", OSBoolean::withBoolean(sensor_query->has_8bit_w));
        tuningProps2->setObject("Has Adjustable Mapping", OSBoolean::withBoolean(sensor_query->has_adjustable_mapping));
        tuningProps2->setObject("Has Info2 (Query 14 present)", OSBoolean::withBoolean(sensor_query->has_info2));
        tuningProps2->setObject("Has Physical Properties", OSBoolean::withBoolean(sensor_query->has_physical_props));
        tuningProps2->setObject("Has Finger Limit", OSBoolean::withBoolean(sensor_query->has_finger_limit));
        tuningProps2->setObject("Has Linear Coefficient 2", OSBoolean::withBoolean(sensor_query->has_linear_coeff_2));
        setProperty("Tuning (Query 12)", tuningProps2);
        
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
        
        jitterProps = OSDictionary::withCapacity(2);
        jitterProps->setObject("Jitter Window Size", OSNumber::withNumber(sensor_query->jitter_window_size, 8));
        jitterProps->setObject("Jitter Filter Type", OSNumber::withNumber(sensor_query->jitter_filter_type, 8));
        setProperty("Jitter", jitterProps);
        
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
    
        miscProps = OSDictionary::withCapacity(5);
        miscProps->setObject("Light Control", OSNumber::withNumber(sensor_query->light_control, 8));
        miscProps->setObject("Clickpad Properties", OSNumber::withNumber(sensor_query->clickpad_props, 8));
        miscProps->setObject("Mouse Buttons", OSNumber::withNumber(sensor_query->mouse_buttons, 8));
        miscProps->setObject("Is Clear", OSBoolean::withBoolean(sensor_query->is_clear));
        miscProps->setObject("Has Advanced Gestures", OSBoolean::withBoolean(sensor_query->has_advanced_gestures));
        setProperty("Misc", miscProps);
        
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
        
        sizeProps = OSDictionary::withCapacity(2);
        sizeProps->setObject("X Sensor Size (mm)", OSNumber::withNumber(sensor_query->x_sensor_size_mm, 16));
        sizeProps->setObject("Y Sensor Size (mm)", OSNumber::withNumber(sensor_query->y_sensor_size_mm, 16));
        setProperty("Size", sizeProps);
        
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
        IOLogError("F11: Could not read Query Base Addr\n");
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
        sensor.x_mm = sens_query.x_sensor_size_mm;
        sensor.y_mm = sens_query.y_sensor_size_mm;
    } else {
        IOLogError("No size data from Device.\n");
        return -ENODEV;
    }
    
    if (!sens_query.has_abs) {
        IOLogError("No absolute reporting support!");
        return -ENODEV;
    }
    
    rc = rmiBus->readBlock(control_base_addr + F11_CTRL_SENSOR_MAX_X_POS_OFFSET,
                           (u8 *)&max_x_pos, sizeof(max_x_pos));
    if (rc < 0) {
        IOLogError("F11: Could not read max x\n");
        return rc;
    }
    
    rc = rmiBus->readBlock(control_base_addr + F11_CTRL_SENSOR_MAX_Y_POS_OFFSET,
                           (u8 *)&max_y_pos, sizeof(max_y_pos));
    if (rc < 0) {
        IOLogError("F11: Could not read max y\n");
        return rc;
    }
        
    sensor.max_x = max_x_pos;
    sensor.max_y = max_y_pos;
    
    rc = f11_2d_construct_data();
    if (rc < 0) {
        IOLogError("F11: Could not construct 2d Data\n");
        return rc;
    }
    
    if (has_acm)
        sensor.attn_size += sensor.nbr_fingers * 2;
    
    rc = f11_read_control_regs(&dev_controls,
                               control_base_addr);
    if (rc < 0) {
        IOLogError("Failed to read F11 control params.\n");
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
        IOLogError("F11: Failed to write control registers\n");
    
    return 0;
}

void F11::setThumbFingerType(int fingers)
{
    int lowestFingerIndex = -1;
    UInt32 minY = 0;
    for (int i = 0; i < fingers; i++) {
        auto &trans = inputEvent.transducers[i];
        
        if (trans.isValid && trans.currentCoordinates.y > minY) {
            minY = trans.currentCoordinates.y;
            lowestFingerIndex = i;
        }
    }
    
    if (lowestFingerIndex == -1)
        IOLogError("LowestFingerIndex = -1 When there are 4 fingers");
    else {
        auto &trans = inputEvent.transducers[lowestFingerIndex];
        if (trans.fingerType != kMT2FingerTypeUndefined)
            freeFingerType[trans.fingerType] = true;
        
        trans.fingerType = kMT2FingerTypeThumb;
        freeFingerType[kMT2FingerTypeThumb] = false;
    }
}

MT2FingerType F11::getFingerType(VoodooInputTransducer *transducer)
{
    for (MT2FingerType i = kMT2FingerTypeIndexFinger; i < kMT2FingerTypeCount; i = (MT2FingerType)(i + 1)) {
        if (freeFingerType[i]) {
            freeFingerType[i] = false;
            return i;
        }
    }
    
    return kMT2FingerTypeUndefined;
}
