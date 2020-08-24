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

    IOLogDebug("RMI configuration %s loaded: %x -> %x\n", configurationKey, *defaultValue, value->getValue());
    *defaultValue = value->getValue();
    return true;
}

bool Configuration::loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 *defaultValue) {
    OSNumber* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSNumber, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("RMI configuration %s loaded: %llx -> %llx\n", configurationKey, *defaultValue, value->unsigned64BitValue());
    *defaultValue = value->unsigned64BitValue();
    return true;
}

bool Configuration::loadUInt32Configuration(OSDictionary *dict, const char* configurationKey, UInt32 *defaultValue) {
    OSNumber* value;
    if (!dict || nullptr == (value = OSDynamicCast(OSNumber, dict->getObject(configurationKey))))
        return false;

    IOLogDebug("RMI configuration %s loaded: %x -> %x\n", configurationKey, *defaultValue, value->unsigned32BitValue());
    *defaultValue = value->unsigned32BitValue();
    return true;
}
