# VoodooRMI (WIP)

A port for macOS of Synaptic's RMI code from Linux. It is for touchscreens, touchpads, and other sensors. Many PS2 trackpads and sensors support other buses like I2C or SMBus, though SMBus is advantageous for macOS due to not requiring pinning.

This communicates over SMBus or I2C (not implemented yet).

## Currently Working  
* Force Touch emulation for clickpads (press down clickpad and increase area finger uses)
* Up to four finger gestures (Though it can track up to 5 fingers)
* Buttons
* Trackstick
* Power Management
* SMBus Communication

## WIP
* I2C Communication

## How do I know if my device is compatible?
**SMBus**
Windows: Check under Device Manager for a Synaptics SMBus device
Linux:
* If you are using intertouch (i.e. psmouse.intertouch=1), then it's compatible
* Get `i2c-tools` from your package manager. Run `i2cdetect -l`, and note the number for SMBus (It's usually zero). Run `i2cdetect #` where # is the number you got from running the prior command. Synaptic devices are always at address 0x2c, so check at that address for anything other than `--`. It will usually appear as `UU` in my experiance if it's a Synaptics device.

**I2C**
Linux:
* Check for the presence of RMI4 in `dmesg`.
* Get `i2c-tools` from your package manager, and use the `i2cdetect` tool to see if there are any devices at address 0x2c for any bus that isn't SMBus. If you see it under SMBus, I'd use SMBus as it doesn't require any pinning!

## Requirements

**SMBus**
* [VoodooSMBus](https://github.com/VoodooSMBus/VoodooSMBus)
  * Apple's SMBus **PCI** controller cannot load, as it interfers with VoodooSMBus.
* You likely want VoodooPS2 for keyboard as well. Make sure VoodooPS2Mouse/VoodooPS2Trackpad does not load.
  * OpenCore users can just disable Mouse/Trackpad in their config.plist.
  * Clover users - go inside the VoodooPS2 kext and remove Mouse/Trackpad from the PlugIns folder.

## Installation
1) Add the required kexts to your bootloader
2) Disable VoodooPS2Mouse, VoodooPS2Trackpad, and if applicable, the VoodooInput from within the PS2 kext.
3) For OpenCore users, make sure to add VoodooInput as well, it's under `VoodooRMI.kext/Contents/PlugIns/VoodooInput.kext`
