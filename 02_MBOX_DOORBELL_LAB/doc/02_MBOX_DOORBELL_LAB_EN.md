# Lab 02 - MBOX Doorbell (signal-only, bidirectional)

## 1. Purpose of this lab

This is the first lab in the series where the two cores actually talk to each other. It uses Zephyr's **MBOX API** (`mbox.h`) purely as a "doorbell" - a signal with no data payload at all. `mbox_send_dt()` is called with `NULL` instead of real data, so only the fact that "it rang" ever crosses over, nothing more.

- **PROCPU → APPCPU**: pressing the board's onboard BOOT button on procpu rings appcpu's doorbell (visible via LED)
- **APPCPU → PROCPU**: appcpu rings procpu's doorbell on its own every 3 seconds (visible via log)
- Both cores keep blinking their Lab 01 heartbeat LEDs the whole time

## 2. `&mbox0` alone is enough - no `&ipm0` needed

Lab 01 established: **unless `&ipm0` is enabled on BOTH the PROCPU and APPCPU images, APPCPU never comes out of reset.**

But looking at ESP32-S3's devicetree, `&ipm0` and `&mbox0` are not two separate pieces of hardware - they are **two different Zephyr driver bindings for the exact same physical hardware**: identical (adjacent) register block, the same shared-memory region, and the same interrupt sources (FROM_CPU_INTR0/1). In other words, this single "inter-core interrupt/mailbox" peripheral can be addressed either through the legacy API (`ipm.h`/`&ipm0`) or the modern one (`mbox.h`/`&mbox0`).

Confirmed on real hardware: **enabling `&mbox0` alone (with `&ipm0` left disabled) is enough for APPCPU to come out of reset and run normally.** So what actually matters for waking APPCPU isn't the specific `ipm0` driver - it's this shared hardware block being enabled at all, through whichever binding.

Based on this finding, this series' policy is now: **a dual-core/IPC lab only needs to enable whichever of `ipm0` / `mbox0` matches the API it's actually teaching** - not both. Enabling both at once is avoided, since that would mean two drivers competing for the same interrupt sources. This lab teaches the MBOX API, so it enables `&mbox0` only.

## 3. Parts / wiring

| Part | Qty |
| --- | --- |
| ESP32-S3-DevKitC-1 | 1 |
| LED | 2 (reused from Lab 01) |
| Resistor (220-330 ohm) | 2 |
| Breadboard + jumper wires | a few |

| Signal | GPIO | Purpose |
| --- | --- | --- |
| PROCPU heartbeat LED | **GPIO2** | Same as Lab 01 |
| APPCPU heartbeat LED | **GPIO42** | Same as Lab 01 |
| PROCPU trigger button | **GPIO0 (BOOT button)** | **No extra wiring needed** - reuses the DevKitC-1's own onboard BOOT button, already exposed by Zephyr's board files as the `sw0` alias |

This lab reuses Lab 01's LED wiring as-is, and the button is already on the board, so **there is no additional wiring at all.**

## 4. Directory layout

```
02_MBOX_DOORBELL_LAB/
├── doc/
│   ├── 02_MBOX_DOORBELL_LAB_KR.md
│   └── 02_MBOX_DOORBELL_LAB_EN.md   (this document)
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

## 5. Build & flash

```bash
cd 02_MBOX_DOORBELL_LAB/lab

# Build both images (procpu + appcpu) together
west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .

# Flash (both images go on in one shot)
west flash
```

- If you forget `--sysbuild`, only the procpu image gets built and `remote/` is silently ignored.
- Open a serial terminal (115200bps) as usual to see PROCPU's log output. APPCPU still has no console at all (see Lab 01).

## 6. How the code works

**PROCPU (`src/main.c`)**
- Keeps blinking the GPIO2 LED every 500ms (same heartbeat as Lab 01)
- Polls the BOOT button (GPIO0) every 20ms and, on the press edge, calls `mbox_send_dt(&tx_channel, NULL)` to ring appcpu's doorbell - signal only, no data
- Always listens for appcpu's doorbell via `mbox_register_callback_dt()` - each time one arrives, increments a counter and logs it

**APPCPU (`remote/src/main.c`)**
- Keeps blinking the GPIO42 LED every 200ms as usual (same heartbeat as Lab 01)
- When procpu's doorbell arrives, briefly interrupts the normal blink for a fast triple-flash (6 toggles at 80ms) before returning to the normal rate
- Independently of that, rings procpu's doorbell on its own via `mbox_send_dt(&tx_channel, NULL)` every 3 seconds (with no console, this is only observable from procpu's log output)

**Devicetree (`boards/*.overlay`)**
- Both images enable the MBOX hardware with `&mbox0 { status = "okay"; };`
- Each image adds an `mbox-consumer` node with `mboxes`/`mbox-names` properties naming the "tx"/"rx" channels - the same pattern Zephyr's own MBOX sample uses, via `compatible = "vnd,mbox-consumer"` (a test binding already built into mainline Zephyr)
- ESP32's MBOX hardware only has one channel per direction, so both tx and rx point at `&mbox0 0` - the actual direction is determined by which core the image is running on, not by the channel number

## 7. Checklist for correct behavior

- [ ] The LED on GPIO2 keeps blinking at 0.5s intervals (PROCPU heartbeat)
- [ ] The LED on GPIO42 keeps blinking at 0.2s intervals (APPCPU heartbeat)
- [ ] The serial terminal shows `02_MBOX_DOORBELL_LAB (core0/procpu) starting`
- [ ] Pressing the **BOOT button** prints `button pressed - ringing appcpu's doorbell` on the serial terminal, and shortly after, **the APPCPU LED does a fast triple-flash** before returning to its normal blink rate
- [ ] Even without pressing the button, `doorbell from appcpu received (count=N)` keeps appearing on the serial terminal **every 3 seconds**, with N increasing each time

If this lab works correctly, you've completed the most basic IPC pattern where two cores communicate purely through signals ("doorbells"). Starting with Lab 03, the two cores begin exchanging actual data over shared memory as well.
