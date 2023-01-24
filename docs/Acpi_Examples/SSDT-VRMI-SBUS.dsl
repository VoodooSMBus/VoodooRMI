/*
 * Example of how to configure VoodooRMI when using
 * an SMBus trackpad.
 *
 * Configuration values can be found in the README
 */

#define SBUS_DEVICE \_SB.PCI0.SBUS

DefinitionBlock ("", "SSDT", 2, "GWYD", "Set", 0) {
    External (SBUS_DEVICE, DeviceObj)
    Scope (SBUS_DEVICE) {
        Name (RCFG, Package() {
            // Disable force touch
            // 0 = Disabled
            // 1 = Clickpad button + Size threshold
            // 2 = Size threshold 
            "ForceTouchType", 0,
        })
    }
}
