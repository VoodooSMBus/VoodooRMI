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

#define setPropertyBoolean(dict, name, boolean) dict->setObject(name, boolean ? kOSBooleanTrue : kOSBooleanFalse)
// define a OSNumber(OSObject) *value before use
#define setPropertyNumber(dict, name, number, bits) value = OSNumber::withNumber(number, bits); if (value != nullptr) {dict->setObject(name, value); value->release();}
#define setPropertyString(dict, name, str) value = OSString::withCString(str); if (value != nullptr) {dict->setObject(name, value); value->release();}

class Configuration {
    
public:
    static bool loadBoolConfiguration(OSDictionary *dict, const char* configurationKey, bool *defaultValue);
    static bool loadUInt32Configuration(OSDictionary *dict, const char *configurationKey, UInt32 *defaultValue);
    static bool loadUInt64Configuration(OSDictionary *dict, const char* configurationKey, UInt64 *defaultValue);
    
private:
    Configuration() {}

};

#endif /* Configuration_hpp */
