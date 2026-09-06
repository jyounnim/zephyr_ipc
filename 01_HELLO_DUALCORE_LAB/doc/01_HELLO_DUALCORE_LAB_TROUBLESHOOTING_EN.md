# Lab 01 Troubleshooting - APPCPU LED never lights up

## Symptom

The PROCPU LED (and its log output) worked correctly from the start. The APPCPU LED never lit up at all. Tried multiple GPIO pins (GPIO42 -> GPIO18 -> GPIO2), different blink periods, and physically swapping the LED/resistor pair between the two circuits - always the same result: **only the pin driven by PROCPU ever toggles; whichever pin APPCPU drives never toggles, regardless of which pin that is.**

## Causes ruled out (in order)

1. **A bad LED or resistor** - ruled out. Swapping the same LED+resistor pair between the GPIO2 (PROCPU) and GPIO42/GPIO18 (APPCPU) circuits always showed toggling only when connected to the PROCPU-driven pin.
2. **A problem with the GPIO42 pin itself, or a mistake in the `&gpio1` (pins 32+) local-index math** - ruled out. When PROCPU was made to drive GPIO18 (`&gpio0`) instead of GPIO2, and later GPIO2 itself, both worked fine. So the pin/controller was never the issue. (For the record, the local-index formula for `&gpio1` - physical GPIO number minus 32 - was confirmed correct, e.g. GPIO42 -> `<&gpio1 10 ...>`.)
3. **Stale build cache / flash artifacts** - ruled out. A full `rm -rf build` clean rebuild, and even a full `esptool erase_flash` followed by reflashing, produced the identical symptom.
4. **Something wrong in the build/flash process itself** - ruled out. The `west build`/`west flash` logs showed both images (procpu at `0x00000000`, appcpu/remote at `0x002c0000`) building without errors and being written to their own distinct flash addresses correctly.
5. **A Kconfig difference (e.g. logging)** - ruled out. Adding `CONFIG_LOG`/`CONFIG_LOG_MODE_IMMEDIATE` made no difference.

## Actual root cause

**Unless `&ipm0` (the ESP32 soft IPM mailbox) is enabled (`status = "okay"`) on BOTH the PROCPU and APPCPU images, the APPCPU core itself never comes out of reset.** This has nothing to do with GPIO pin numbers or wiring at all - it's a property of how this board/Zephyr combination boots the second core.

This never surfaced in this project's other lab (`06_AHT20_BMP280_MultiSensor`, which actually uses IPM for real) because that lab was already using IPM from the start. Lab 01 was designed as a pure "hello world" with no IPC at all, so IPM was left out on the assumption it wasn't needed - and that omission turned out to be exactly why APPCPU was never running.

### How this was narrowed down

1. PROCPU and APPCPU's code used the exact same pattern (a `gpio-leds` devicetree node + `gpio_pin_configure_dt`/`gpio_pin_set_dt`), yet only one side failed - this pointed away from a code-logic bug and toward "is this code even running at all?"
2. Having PROCPU drive the same pin (GPIO18) confirmed the pin itself was fine - narrowing the problem down to whether APPCPU was executing at all.
3. Re-diffing Lab 01's configuration against the already hardware-verified `06_AHT20_BMP280_MultiSensor` lab found exactly one remaining substantive difference: whether IPM (`&ipm0`, `CONFIG_IPM`, `CONFIG_ESP32_SOFT_IPM`) was enabled.
4. Enabling IPM (without ever actually using it) as a test immediately fixed the APPCPU LED - confirming the root cause.

## Fix / final configuration

This lab (and every later lab in this series) keeps the following in `prj.conf` and the overlays on both images by default, regardless of whether that particular lab actually uses IPC:

**Both `prj.conf` files (procpu and appcpu):**
```
CONFIG_IPM=y
CONFIG_ESP32_SOFT_IPM=y
```

**Both overlays (procpu and appcpu):**
```
&ipm0 {
	status = "okay";
};
```

## Open question for later labs

- Starting with Lab 02, this series moves to the MBOX (`mbox.h`) API. **Whether this same IPM requirement still applies once MBOX is in use - or whether enabling MBOX itself has the same effect of releasing APPCPU - has not been confirmed yet.** This needs to be re-checked at the start of Lab 02; it's possible both IPM and MBOX will need to be enabled together.
