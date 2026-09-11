# Lab 04 - IPC Service (icmsg backend): Rebuilding Lab 03 on a Standard Framework

## 1. Purpose of this lab

Lab 03 implemented IPC using nothing but raw MBOX and a hand-rolled shared struct, and directly confirmed a fundamental limitation as a result: "notification counts can be preserved, but a single-slot shared memory region still loses intermediate data".

Lab 04 rebuilds the exact same experiment from Lab 03 (procpu keeps sending an incrementing sequence number, appcpu echoes it straight back), this time using Zephyr's standard `IPC Service` framework (`ipc_service.h`) with the **icmsg backend**. The goals are two-fold:

- Get familiar with IPC Service's **instance/endpoint model** and its **callback-based API** (`bound`/`received`/`error`)
- Directly confirm **how icmsg solves the data-loss problem** Lab 03 ran into, at the framework level (icmsg is a real queued ring buffer, not a single slot, so none of the values procpu sends should ever go missing along the way)

Again, no button and no extra wiring - it uses the same two LEDs as before (PROCPU=GPIO2, APPCPU=GPIO42).

## 2. Where to put the shared memory - discovering `shm0`

Unlike Lab 02/03's MBOX usage, the icmsg backend needs **its own separate shared-memory region** (MBOX is still used purely as a "new data available" signal here too - the actual message bytes travel through this separate region's ring buffer). Lab 03's reused `ipmmem0` is only 1KB, and it's already reserved by the `&ipm0`/`&mbox0` drivers for their own purpose, so it felt a bit awkward to put to a new use here.

Looking again at ESP32-S3's SoC devicetree (`esp32s3_common.dtsi`), though, we found this node **already declared** right after `ipmmem0`:

```c
ipmmem0: memory@3fce5000 {
    compatible = "mmio-sram";
    reg = <0x3fce5000 0x400>;   /* 1KB - used by Lab 02/03 */
};

shm0: memory@3fce5400 {
    compatible = "mmio-sram";
    reg = <0x3fce5400 0x4000>;  /* 16KB - not used by any lab so far! */
};
```

`shm0` ("shared memory 0") is a separate **16KB block**, declared using the exact same pattern as `ipmmem0`. The name alone reads as "general-purpose shared memory", and its placement - immediately before the `&ipm0`/`&mbox0` registers (`0x3fce9400`), just like `ipmmem0` - strongly suggests the SoC port maintainers carved this block out specifically for inter-core sharing. (For reference, the original ESP32's `esp32_common.dtsi` has `ipmmem0`/`shm0` declared in the exact same pattern too - this looks like a deliberate convention from the SoC port maintainers.)

**To be upfront about it**: we could not find any existing sample or test in mainline Zephyr that actually uses `shm0`. So this wasn't a case of "following an already-verified example" - we inferred it was safe because it's declared exactly the same way as `ipmmem0`, the block right next to it that this series has already verified on real hardware, and **we have now confirmed on a real DevKitC-1 board that this design works exactly as designed** (see section 9).

We split this 16KB in half (8KB, 8KB) for icmsg's tx/rx ring buffers (Nordic's official sample only uses 2KB per direction, so 8KB leaves quite a bit of headroom).

## 3. Devicetree design

procpu overlay:

```c
reserved-memory {
    #address-cells = <1>;
    #size-cells = <1>;

    icmsg_p2a: memory@3fce5400 { reg = <0x3fce5400 0x2000>; }; /* procpu -> appcpu */
    icmsg_a2p: memory@3fce7400 { reg = <0x3fce7400 0x2000>; }; /* appcpu -> procpu */
};

ipc0: ipc0 {
    compatible = "zephyr,ipc-icmsg";
    dcache-alignment = <0>;
    tx-region = <&icmsg_p2a>;
    rx-region = <&icmsg_a2p>;
    mboxes = <&mbox0 0>, <&mbox0 0>;
    mbox-names = "tx", "rx";
    status = "okay";
};
```

The appcpu overlay swaps `tx-region`/`rx-region` exactly (procpu's tx becomes appcpu's rx). Everything else is identical.

A few notes on the design choices:

- **`dcache-alignment = <0>`**: this is a required property per the official binding docs, so it must be set to something. We set it to `0` (meaning "this region is not cached") because Labs 02/03 already read and wrote the adjacent block (`ipmmem0`) via plain `volatile` pointers with no cache flush/invalidate at all, and it worked correctly on real hardware - implying `shm0`, in the same SRAM range, is not cached either.
- **`mboxes = <&mbox0 0>, <&mbox0 0>;`**: as already established in Labs 02/03, ESP32's MBOX hardware only has one channel per direction, so both tx and rx point at channel 0. icmsg internally uses the exact same `mbox_send_dt()`/`mbox_register_callback_dt()` pattern we used directly in Labs 02/03, so this carries over unchanged.
- **Only `&mbox0` enabled, `&ipm0` stays disabled**: following this series' existing policy.
- icmsg is **single-endpoint only** (per `CONFIG_IPC_SERVICE_BACKEND_ICMSG`'s Kconfig description: "single endpoint implementation based on circular packet buffer") - so this lab registers exactly one endpoint per instance. Multiple endpoints would require a different backend, such as Lab 05's rpmsg or `icmsg_me`.

## 4. What you need / wiring

- An ESP32-S3-DevKitC-1 board and one USB cable
- No extra wiring (uses the same two LEDs as Labs 01/02/03)

## 5. Directory structure

```
04_IPC_SERVICE_ICMSG_LAB/
├── doc/
│   ├── 04_IPC_SERVICE_ICMSG_LAB_KR.md
│   └── 04_IPC_SERVICE_ICMSG_LAB_EN.md   (this document)
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── sysbuild.cmake
    ├── sysbuild.conf
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   ├── common.h                      (message struct definition)
    │   └── main.c                        (procpu application)
    └── remote/
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            ├── common.h                  (byte-for-byte identical copy of the procpu one)
            └── main.c                    (appcpu application)
```

## 6. Build & flash

```bash
cd 04_IPC_SERVICE_ICMSG_LAB/lab

west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .
west flash
```

After building, open a serial terminal (115200bps) and watch procpu's log.

## 7. How the code works

### 7.1 `common.h`

```c
struct app_msg {
    uint32_t seq;
};
```

Both directions - procpu → appcpu and appcpu → procpu - exchange nothing but this one 4-byte struct. It's duplicated identically in `src/` and `remote/src/` (same reason as Lab 03's `shared_mem.h` - procpu and appcpu are two completely separate Zephyr applications built for different boards).

### 7.2 procpu (`src/main.c`)

1. Gets the icmsg instance via `DEVICE_DT_GET(DT_NODELABEL(ipc0))` and opens it with `ipc_service_open_instance()`
2. Registers a single endpoint with `ipc_service_register_endpoint()`, specifying three callbacks:
   - `bound`: called once both sides confirm the connection - releases a semaphore so `main()` can proceed past its wait
   - `received`: called every time appcpu's echoed reply arrives - logged via `LOG_INF("recv reply seq=...")`
   - `error`: called on an internal icmsg error (should never fire during normal operation)
3. After waiting for bind, the main loop:
   - Toggles the GPIO2 LED every 500ms (same as before)
   - Sends a new sequence number every 200ms via `ipc_service_send()`. **If it returns `-ENOMEM`** (icmsg's ring buffer is full), the value is not discarded - it is **retried with the same value** on the next tick, rather than being silently overwritten and lost the way Lab 03's single shared slot would have - the attitude here is "wait for room and send it, no matter what."

### 7.3 appcpu (`remote/src/main.c`)

- Echoes the received value straight back via `ipc_service_send()` **directly from inside** the `received` callback (not deferred to a separate main loop)
- Why calling `ipc_service_send()` from inside the callback is safe: after reading Zephyr's icmsg implementation (`subsys/ipc/ipc_service/lib/icmsg.c`), we confirmed that with multithreading enabled (true of every lab in this series), the `received` callback is invoked from a **workqueue thread context**, and no lock is held while calling it - so calling `ipc_service_send()` again from inside it cannot deadlock.
- The LED blinks its normal 200ms heartbeat, with a quick double-flash every time it echoes a message back - this is a purely cosmetic signal, so if flash timings happen to overlap and a few look coalesced, that does not by itself mean a message was lost (the real proof is in procpu's log).

## 8. Checklist for correct behavior

- [ ] The LED on GPIO2 keeps blinking at 0.5s intervals
- [ ] The LED on GPIO42 normally blinks at 0.2s intervals, with a quick double-flash every time it echoes a message back
- [ ] The serial terminal shows `04_IPC_SERVICE_ICMSG_LAB (core0/procpu) starting`, followed by `endpoint bound` (if binding never happens, the log stops here - double-check the devicetree)
- [ ] `sent seq=N (total_sent=N)` keeps appearing and increasing roughly every 200ms
- [ ] `recv reply seq=N (total_received=N, ...)` follows right after each one
- [ ] **The most important check**: `recv reply seq=` values do NOT skip the way Lab 03's did - they continue **1,2,3,4,5... with none missing** (the opposite result from Lab 03 is what correct behavior looks like here)
- [ ] No `icmsg error:` or `ipc_service_send() failed` errors appear (a `send buffer full, retrying` warning may or may not appear - if it does, it's fine as long as the same value succeeds on the very next line)

## 9. Confirmed on real hardware

All of the checklist items above were confirmed on a real DevKitC-1 board. Here is the actual log about 55 seconds after boot:

```
[00:00:55.444,000] <inf> app_procpu: recv reply seq=268 (total_received=268, total_sent=268)
[00:00:55.444,000] <inf> app_procpu: sent seq=268 (total_sent=268)
[00:00:55.650,000] <inf> app_procpu: recv reply seq=269 (total_received=269, total_sent=269)
[00:00:55.650,000] <inf> app_procpu: sent seq=269 (total_sent=269)
[00:00:55.856,000] <inf> app_procpu: recv reply seq=270 (total_received=270, total_sent=270)
[00:00:55.856,000] <inf> app_procpu: sent seq=270 (total_sent=270)
...
[00:00:58.537,000] <inf> app_procpu: recv reply seq=283 (total_received=283, total_sent=283)
[00:00:58.537,000] <inf> app_procpu: sent seq=283 (total_sent=283)
```

This shows exactly the opposite result from Lab 03, as expected:

- **`recv reply seq=` continues 268, 269, 270, 271... with absolutely nothing skipped.** The exact opposite of Lab 03, where `last_seen_seq` jumped 445 → 446 → 448.
- **`total_received` always exactly equals `total_sent`** (`recv reply seq=268 (total_received=268, ..., total_sent=268)`). Unlike Lab 03, where `drained_count` (261) and `last_seen_seq` (455) kept drifting apart, the "sent count" and "received count" never diverge here even once.
- No `icmsg error:` log or `send buffer full` warning appeared during this window at all - meaning the 200ms send rate comfortably fits within icmsg's ring buffer capacity (8KB).

This confirms, on real hardware, that reusing `shm0` (the 16KB block with no prior usage example in mainline Zephyr) as icmsg's tx/rx region, and setting `dcache-alignment = <0>`, both work correctly.

## What's next

Lab 05 swaps the same request-response structure from the icmsg backend to the `rpmsg` backend (OpenAMP-based, the standard RPMsg protocol), comparing latency and code complexity between the two.
