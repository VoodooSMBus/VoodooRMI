# VoodooRMI (WIP)

A port for macOS of Synaptic's RMI code from Linux. It is for touchscreens, touchpads, and other sensors. Many PS2 trackpads and sensors support other buses like I2C or SMBus, though SMBus is advantageous for macOS due to not requiring pinning.

This communicates over SMBus or I2C (not implemented).

## Requirements

**SMBus**
* [VoodooSMBus](https://github.com/VoodooSMBus/VoodooSMBus)
* You likely want VoodooPS2 for keyboard as well. Make sure VoodooPS2Mouse/VoodooPS2Trackpad does not load.
  * OpenCore users can just disable Mouse/Trackpad in their config.plist.
  * Clover users - go inside the VoodooPS2 kext and remove Mouse/Trackpad from the PlugIns folder. 
