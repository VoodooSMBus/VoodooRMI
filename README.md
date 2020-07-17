# VoodooRMI

A port for macOS of Synaptic's RMI code from Linux. It is for touchscreens, touchpads, and other sensors. Many PS2 trackpads and sensors support other buses like I2C or SMBus, though SMBus is advantageous for macOS due to not requiring ACPI edits.

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
Windows:
* Check under Device Manager for a Synaptics SMBus device

Linux:
* If you are using intertouch (i.e. psmouse.intertouch=1) for your synaptics trackpad, then it's compatible
* Get `i2c-tools` from your package manager. Run `i2cdetect -l`, and note the number for SMBus (It's usually zero). Run `i2cdetect #` where # is the number you got from running the prior command. Synaptic devices are always at address 0x2c, so check at that address for anything other than `--`. It will usually appear as `UU` in my experiance if it's a Synaptics device.
  * I've seen one or two examples where there was a trackpad that supported SMBus but nothing appeared at the address (Wack!)
* Likely compatible if you run `dmesg` and find a message along the lines of `"Your touchpad x says it can support a different bus."` and it's a synaptics trackpad.

**I2C** (Not working)  
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
3) For OpenCore users, make sure to add VoodooInput, VoodooTrackpoint, and RMISMBus/RMII2C as well to your Config.plist, it's under `VoodooRMI.kext/Contents/PlugIns/`

Note: If you change any configuration values in the Info.plist, they must be integers/whole numbers

## Configuration

The values below can be edited under Info.plist within the kext itself - these can be changed without recompiling  
Note that using non-integer values causes undefined behaviour which may prevent the kext from loading

| Value | Default | Description |
| ----- | ------- | ----------- |
| `ForceTouchEmulation` | True | Allows Force Touch emulation on Clickpads |
| `ForceTouchMinPressure` | 90 | Minimum z value to trigger Force touch when clickpad is clicked |
| `DisableWhileTypingTimeout` | 100 | Milliseconds after typing in which to reject touchpad packets |
| `TrackstickMultiplier` | 20 | Multiplier used on trackstick inputs (other than scrolling). This is divided by 20, so the default value of 20 will not change the output value at all |
| `TrackstickScrollMultiplierX` | 20 | Multiplier used on the x access when middle button is held down for scrolling. This is divded by 20. |
| `TrackstickScrollMultiplierY` | 20 | Same as the above, except applied to the Y axis |
| `TrackstickDeadzone` | 1 | Minimum value at which trackstick reports will be accepted. This is subtracted from the input of the trackstick, so setting this extremely high will reduce trackstick resolution |
| `MinYDiffThumbDetection` | 200 | Minimum distance between the second lowest and lowest finger in which Minimum Y logic is used to detect the thumb rather than using the z value from the trackpad. Setting this higher means that the thumb must be farther from the other fingers before the y coordinate is used to detect the thumb, rather than using finger area. Keeping this smaller is preferable as finger area logic seems to only be useful when all 4 fingers are grouped together closely, where the thumb is more likely to be pressing down more |

## Building
1) `git submodule init`
2) `git submodule update`
3) Build within XCode
