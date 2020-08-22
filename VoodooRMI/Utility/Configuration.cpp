/*
 * Configuration.cpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#include "Configuration.hpp"


bool Configuration::loadBoolConfiguration(OSDictionary *dict, const char* configurationKey, bool *defaultValue) {
    if (!dict) return false;
    
    OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject(configurationKey));
    if (value != nullptr) {
        IOLog("RMI configuration %s loaded: %x", configurationKey, value->getValue());
        *defaultValue = value->getValue();
        return true;
    }
    
    return false;
}

bool Configuration::loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 *defaultValue) {
    if (!dict) return false;
    
    OSNumber* value = OSDynamicCast(OSNumber, dict->getObject(configurationKey));
    if (value != nullptr) {
        IOLog("RMI configuration %s loaded: %llx", configurationKey, value->unsigned64BitValue());
        *defaultValue = value->unsigned64BitValue();
        return true;
    }
    
    return false;
}

bool Configuration::loadUInt32Configuration(OSDictionary *dict, const char* configurationKey, UInt32 *defaultValue) {
    if (!dict) return false;
    
    OSNumber* value = OSDynamicCast(OSNumber, dict->getObject(configurationKey));
    if (value != nullptr) {
        IOLog("RMI configuration %s loaded: %x", configurationKey, value->unsigned32BitValue());
        *defaultValue = value->unsigned32BitValue();
        return true;
    }
    
    return false;
}
