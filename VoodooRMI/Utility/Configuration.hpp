/*
 * Configuration.hpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#ifndef Configuration_hpp
#define Configuration_hpp 

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include "./Logging.h"

#define setPropertyBoolean(dict, name, boolean) \
    do { dict->setObject(name, boolean ? kOSBooleanTrue : kOSBooleanFalse); } while (0)

// define a OSNumber(OSObject) *value before use
#define setPropertyNumber(dict, name, number, bits) \
    do { \
        value = OSNumber::withNumber(number, bits); \
        if (value != nullptr) { \
            dict->setObject(name, value); \
            value->release(); \
        } \
    } while (0)

#define setPropertyString(dict, name, str) \
    do { \
        value = OSString::withCString(str); \
        if (value != nullptr) { \
            dict->setObject(name, value); \
            value->release(); \
        } \
    } while (0)

class Configuration {
    
public:
    static bool loadBoolConfiguration(OSDictionary *dict, const char* configurationKey, bool *defaultValue);
    static bool loadUInt8Configuration(OSDictionary *dict, const char* configurationKey, UInt8 *defaultValue);
    static bool loadUInt32Configuration(OSDictionary *dict, const char *configurationKey, UInt32 *defaultValue);
    static bool loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 *defaultValue);
    static OSDictionary *mapArrayToDict(OSArray *arr);
    
private:
    Configuration() {}

};

#endif /* Configuration_hpp */
