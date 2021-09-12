/*
 * Configuration.hpp
 * RMI4 Driver for macOS X
 *
 * Copyright (c) 2021 Avery Black <1Revenger1>
 */

#ifndef Logging_h
#define Logging_h

#define IOLogInfo(format, ...) do { IOLog("VRMI - Info:  " format "\n", ## __VA_ARGS__); } while(0)
#define IOLogError(format, ...) do { IOLog("VRMI - Error: " format "\n", ## __VA_ARGS__); } while(0)

#ifdef DEBUG
#define IOLogDebug(format, ...) do { IOLog("VRMI - Debug: " format "\n", ## __VA_ARGS__); } while(0)
#else
#define IOLogDebug(arg...)
#endif // DEBUG

#endif /* Logging_h */
