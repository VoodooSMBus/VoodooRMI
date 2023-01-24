/*
 * Configuration.cpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#include "Configuration.hpp"


bool Configuration::loadBoolConfiguration(OSDictionary *dict, const char* configurationKey, bool *defaultValue) {
    OSBoolean* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSBoolean, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("Config %s loaded: %x -> %x", configurationKey, *defaultValue, value->getValue());
    *defaultValue = value->getValue();
    return true;
}

bool Configuration::loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 *defaultValue) {
    OSNumber* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSNumber, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("Config %s loaded: %llx -> %llx", configurationKey, *defaultValue, value->unsigned64BitValue());
    *defaultValue = value->unsigned64BitValue();
    return true;
}

bool Configuration::loadUInt32Configuration(OSDictionary *dict, const char* configurationKey, UInt32 *defaultValue) {
    OSNumber* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSNumber, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("Config %s loaded: %x -> %x", configurationKey, *defaultValue, value->unsigned32BitValue());
    *defaultValue = value->unsigned32BitValue();
    return true;
}

bool Configuration::loadUInt8Configuration(OSDictionary *dict, const char* configurationKey, UInt8 *defaultValue) {
    OSNumber* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSNumber, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("Config %s loaded: %x -> %x", configurationKey, *defaultValue, value->unsigned8BitValue());
    *defaultValue = value->unsigned8BitValue();
    return true;
}

OSDictionary *Configuration::mapArrayToDict(OSArray *arr) {
    if (!arr)
        return nullptr;

    int pairs = arr->getCount() / 2;
    OSDictionary *dict = OSDictionary::withCapacity(pairs);

    for (int index = 0; index < pairs; index ++) {
        if (OSString *key = OSDynamicCast(OSString, arr->getObject(index * 2))) {
            dict->setObject(key, arr->getObject(index * 2 + 1));
        } else {
            break;
        }
    }
    
    return dict;
}
