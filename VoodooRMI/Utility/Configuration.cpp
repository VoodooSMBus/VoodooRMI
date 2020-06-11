/*
 * Configuration.cpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#include "Configuration.hpp"


bool Configuration::loadBoolConfiguration(OSDictionary *dict, const char* configurationKey, bool defaultValue) {
    if (!dict) return defaultValue;
    
    OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject(configurationKey));
    if (value != nullptr) {
        return value->getValue();
    }
    
    return defaultValue;
}

UInt64 Configuration::loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 defaultValue) {
    if (!dict) return defaultValue;
    
    OSNumber* value = OSDynamicCast(OSNumber, dict->getObject(configurationKey));
    if (value != nullptr) {
        return value->unsigned64BitValue();
    }
    
    return defaultValue;
}

UInt32 Configuration::loadUInt32Configuration(OSDictionary *dict, const char* configurationKey, UInt32 defaultValue) {
    if (!dict) return defaultValue;
    
    OSNumber* value = OSDynamicCast(OSNumber, dict->getObject(configurationKey));
    if (value != nullptr) {
        return value->unsigned32BitValue();
    }
    
    return defaultValue;
}
