/*
 * Configuration.hpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#ifndef Configuration_hpp
#define Configuration_hpp 

#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>

#define DEFAULT_MULT 10

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

// Force touch types
enum RmiForceTouchMode {
    RMI_FT_DISABLE = 0,
    RMI_FT_CLICK_AND_SIZE = 1,
    RMI_FT_SIZE = 2,
};

struct RmiConfiguration {
    /* F03 */
    uint32_t trackpointMult {DEFAULT_MULT};
    uint32_t trackpointScrollXMult {DEFAULT_MULT};
    uint32_t trackpointScrollYMult {DEFAULT_MULT};
    uint32_t trackpointDeadzone {1};
    /* RMI2DSensor */
    uint32_t forceTouchMinPressure {80};
    uint32_t minYDiffGesture {200};
    uint32_t fingerMajorMinorMax {10};
    // Time units are in milliseconds
    uint64_t disableWhileTypingTimeout {2000};
    uint64_t disableWhileTrackpointTimeout {2000};
    // Percentage out of 100
    uint8_t palmRejectionWidth {15};
    uint8_t palmRejectionHeight {80};
    uint8_t palmRejectionHeightTrackpoint {20};
    RmiForceTouchMode forceTouchType {RMI_FT_CLICK_AND_SIZE};
};

// Data for F30 and F3A
struct RmiGpioData {
    bool clickpad {false};
    bool trackpointButtons {true};
};


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
