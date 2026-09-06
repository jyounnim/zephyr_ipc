# Lab 01 - Hello Dual-Core (ESP32-S3 AMP scaffolding, no IPC)

## 1. Purpose of this lab

This is the first lab in the ESP32-S3 Zephyr Dual-Core IPC series. This lab has **no real IPC (no data exchange) at all** - the only goal is to learn how to build and flash **two completely independent Zephyr images** onto ESP32-S3's two cores (PROCPU/APPCPU) at the same time, and confirm that both are actually alive.

- Get comfortable with building both images in one shot via `west build --sysbuild`
- **PROCPU (core0)**: has full UART console support, so it proves itself alive two ways - a blinking LED and `LOG_INF()` messages
- **APPCPU (core1)**: **UART console is not yet supported** - it can only prove itself alive via LED, which is a real, practical constraint of this platform you'll feel directly here
- The two LEDs blink at deliberately different rates, so you can see with your own eyes that both cores are running independently with no coordination between them (that starts in Lab 02)

## 2. Why doesn't APPCPU produce any log output?

Zephyr's ESP32-S3 board support only implements **AMP (Asymmetric Multiprocessing)** - the two cores don't share a single OS image (that would be SMP); instead, `esp32s3_devkitc/esp32s3/procpu` and `esp32s3_devkitc/esp32s3/appcpu` are **completely separate build targets/images**. And as Zephyr's official documentation states, **only PROCPU supports the UART console** (printk/logging) - APPCPU does not, at least not yet.

So in this lab (and every lab after it), there are only two ways to observe what APPCPU is doing:
1. Drive a GPIO (like an LED) directly and watch it
2. Send data to PROCPU over IPC and have PROCPU log it on your behalf (starting Lab 02)

This lab only uses method 1.

## 3. Important: APPCPU won't come out of reset unless IPM is enabled

This lab exchanges no data between the cores at all, yet `prj.conf` and the overlays enable `CONFIG_IPM` / `CONFIG_ESP32_SOFT_IPM` / `&ipm0` on **both** images. That's because of a real-hardware finding on this specific board:

> **Unless `&ipm0` (the ESP32 soft IPM mailbox) is enabled on BOTH the PROCPU and APPCPU images, APPCPU never comes out of reset at all.** No GPIO, no code of any kind runs on it - regardless of pin number or wiring.

In other words, even a pure "hello world" lab like this one with zero actual IPC needs IPM enabled purely to wake APPCPU up. This setting stays on by default in every later lab in this series. (See `01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_EN.md` for how this was diagnosed.)

## 4. Parts / wiring

| Part | Qty |
| --- | --- |
| ESP32-S3-DevKitC-1 | 1 |
| LED | 2 (different colors make them easier to tell apart) |
| Resistor (220-330 ohm) | 2 |
| Breadboard + jumper wires | a few |

| Signal | GPIO | Purpose |
| --- | --- | --- |
| PROCPU heartbeat LED | **GPIO2** | LED anode(+) - resistor - GPIO2, cathode(-) - GND |
| APPCPU heartbeat LED | **GPIO42** | LED anode(+) - resistor - GPIO42, cathode(-) - GND |

- Both GPIO2 and GPIO42 were picked as free GPIOs that don't overlap with strapping pins (0/3/45/46), the USB-JTAG pins (19/20), or the SPI flash/PSRAM range (26-37). This series will keep reusing the "PROCPU heartbeat = GPIO2 / APPCPU heartbeat = GPIO42" convention going forward.
- GPIO42 belongs to the `&gpio1` controller (pins 32 and up), addressed in the overlay via local index 42-32=10. GPIO42 also doubles as part of ESP32-S3's classic 5-pin external JTAG interface (MTMS), but since DevKitC-1 boards use the separate USB-JTAG bridge on GPIO19/20 instead, GPIO42 is free to use as a plain GPIO here.

## 5. Directory layout

```
01_HELLO_DUALCORE_LAB/
├── doc/
│   ├── 01_HELLO_DUALCORE_LAB_KR.md
│   ├── 01_HELLO_DUALCORE_LAB_EN.md              (this document)
│   ├── 01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_KR.md
│   └── 01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_EN.md
└── lab/
    ├── CMakeLists.txt        # core0 (procpu) app
    ├── prj.conf              # core0 config
    ├── sample.yaml
    ├── sysbuild.cmake        # tells sysbuild to also build core1 (remote/)
    ├── sysbuild.conf         # disables MCUboot
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   └── main.c            # core0 source
    └── remote/                # core1 (appcpu) app - a fully independent sub-application
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            └── main.c        # core1 source
```

Because this is an AMP setup, everything under `remote/` is effectively **a completely separate Zephyr application** (its own CMakeLists.txt/prj.conf/overlay/src). `sysbuild.cmake` is what ties the two together under a single `west build` invocation.

## 6. Build & flash

```bash
cd 01_HELLO_DUALCORE_LAB/lab

# Build both images (procpu + appcpu) together
west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .

# Flash (both images go on in one shot)
west flash
```

- If you forget `--sysbuild`, only the procpu image gets built and `remote/` is silently ignored.
- Espressif boards default to also building MCUboot whenever `--sysbuild` is used. This lab is a plain two-image AMP build with nothing to do with OTA, so `sysbuild.conf` turns that off with `SB_CONFIG_BOOTLOADER_NONE=y`.
- Open a serial terminal (115200bps) as usual to see PROCPU's log output. APPCPU has no console at all, as explained above.

## 7. Checklist for correct behavior

- [ ] The LED on GPIO2 blinks at roughly 0.5s intervals (500ms ON / 500ms OFF) - PROCPU
- [ ] The LED on GPIO42 blinks at roughly 0.2s intervals (200ms ON / 200ms OFF) - APPCPU, noticeably faster than the PROCPU LED
- [ ] The serial terminal shows `01_HELLO_DUALCORE_LAB (core0/procpu) starting`, followed by `procpu heartbeat - LED ON/OFF` repeating every 0.5s
- [ ] The two LEDs are NOT synchronized with each other and blink at their own independent rhythms - this is the key thing this lab is meant to show: both cores running completely independently, with no coordination between them

If both LEDs are blinking correctly, this lab is complete. Starting with Lab 02, the two cores will begin actually exchanging signals over MBOX.
