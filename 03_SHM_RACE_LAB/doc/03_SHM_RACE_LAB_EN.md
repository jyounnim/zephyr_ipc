# Lab 03 - Shared Memory (Reusing ipmmem0) + MBOX Notification, and Feeling a Real Race Condition

## 1. Purpose of this lab

Lab 02 used MBOX purely as a "doorbell" that carried no payload at all. Lab 03 takes one step further:

- procpu and appcpu actually **write and read a real struct directly in shared SRAM** (using MBOX + raw pointers, with no framework like `ipc_service.h`)
- Along the way, you observe on real hardware **a race condition that is unavoidable once you build IPC by hand, with no framework underneath**
- You'll see that `atomic_t` fixes "never losing the notification count", but "a single shared-memory slot that keeps getting overwritten with the latest value" still fundamentally loses intermediate values, regardless of the notification count being correct → this is exactly why a real message-queue framework such as `IPC Service` (icmsg/rpmsg) in Lab 04/05 is needed

There is no button and no new wiring in this lab. It reuses the exact same two LEDs as Labs 01/02 (PROCPU=GPIO2, APPCPU=GPIO42) - almost everything you need to observe shows up in **procpu's serial log alone** (appcpu still has no UART console support - see the `01_HELLO_DUALCORE_LAB` doc for why).

## 2. Why reusing `ipmmem0` is safe, instead of defining a new `reserved-memory` region

ESP32-S3's SoC devicetree (`esp32s3_common.dtsi`) already has these nodes:

```c
ipmmem0: memory@3fce5000 {
    compatible = "mmio-sram";
    reg = <0x3fce5000 0x400>;   /* 1024 bytes */
};

ipm0: ipm@3fce9400 {
    compatible = "espressif,esp32-ipm";
    shared-memory = <&ipmmem0>;
    shared-memory-size = <0x400>;
    ...
};

mbox0: mbox@3fce9408 {
    compatible = "espressif,mbox-esp32";
    shared-memory = <&ipmmem0>;
    shared-memory-size = <0x400>;
    ...
};
```

In other words, `ipmmem0` is a 1KB SRAM block that the `&ipm0`/`&mbox0` drivers already reserve for copying message payloads between cores. The natural question is: "is it safe to also use memory that's already reserved for something else?" We verified the answer by reading the actual ESP32 MBOX driver source (`drivers/mbox/mbox_esp32.c`):

```c
if (msg != NULL && msg->data != NULL) {
    uint8_t *dest = (dev_data->other_core_id == 0) ? dev_data->shm.pro_cpu_shm
                                                    : dev_data->shm.app_cpu_shm;
    memcpy(dest, msg->data, msg->size);
}
```

**The driver never reads or writes a single byte of this shared-memory buffer when `msg` is `NULL` or `msg->data` is `NULL`.** Both Lab 02 and this lab always call `mbox_send_dt(&tx_channel, NULL)` - a NULL payload, every time - so the driver's `memcpy` is never executed, and this entire 1KB is completely free for our own application to use. That's why we reuse the existing `ipmmem0` instead of defining a brand-new `reserved-memory` region.

We also confirmed that the `mbox0` driver internally splits this same 1KB into two halves, one per direction (`pro_cpu_shm`/`app_cpu_shm`, 512 bytes each). This lab mirrors that same split for its own traffic (purely for symmetry - it isn't functionally required; see `src/shared_mem.h`).

## 3. What you need / wiring

- An ESP32-S3-DevKitC-1 board and one USB cable
- **No extra wiring** - uses the exact same two onboard LEDs as Labs 01/02:
  - PROCPU heartbeat LED: GPIO2 (`&gpio0`)
  - APPCPU heartbeat LED: GPIO42 (`&gpio1`, local index 42-32=10)
- (If you need to wire the LEDs externally, follow the wiring instructions in the Lab 01 doc - this lab does not repeat that explanation)

## 4. Directory structure

```
03_SHM_RACE_LAB/
├── doc/
│   ├── 03_SHM_RACE_LAB_KR.md
│   └── 03_SHM_RACE_LAB_EN.md           (this document)
└── lab/
    ├── CMakeLists.txt                  (procpu app)
    ├── prj.conf                        (procpu Kconfig)
    ├── sample.yaml
    ├── sysbuild.cmake                  (registers the appcpu image to build alongside)
    ├── sysbuild.conf                   (disables MCUboot)
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   ├── shared_mem.h                 (shared-memory layout definition)
    │   └── main.c                       (procpu application)
    └── remote/                          (appcpu app - a completely separate Zephyr application from procpu)
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            ├── shared_mem.h             (byte-for-byte identical copy of the procpu one - see section 6)
            └── main.c                   (appcpu application)
```

## 5. Build & flash

Exactly like Labs 01/02, use `--sysbuild` to build/flash both the procpu and appcpu images together.

```bash
cd 03_SHM_RACE_LAB/lab

west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .
west flash
```

- `-p always`: fully clears the previous build cache and rebuilds from scratch (avoids mixing up different labs)
- `--sysbuild`: also builds the `remote/` (appcpu) app registered in `sysbuild.cmake`
- `west flash`: flashes the procpu (`0x00000000`) and appcpu/remote (`0x002c0000`) images to their own addresses

After building, open a serial terminal (115200bps) and watch procpu's log.

## 6. How the code works

### 6.1 `shared_mem.h` - shared-memory layout

```c
#define SHM_BASE (DT_REG_ADDR(DT_NODELABEL(ipmmem0)))
#define SHM_SIZE (DT_REG_SIZE(DT_NODELABEL(ipmmem0)))
#define SHM_HALF_SIZE (SHM_SIZE / 2)

#define SHM_P2A_ADDR (SHM_BASE)                 /* procpu -> appcpu */
#define SHM_A2P_ADDR (SHM_BASE + SHM_HALF_SIZE) /* appcpu -> procpu */

struct p2a_payload { uint32_t seq; };
struct a2p_payload { uint32_t drained_count; uint32_t last_seen_seq; };
```

- `DT_NODELABEL(ipmmem0)` reads the address/size of a node that **already exists** in the devicetree. No new devicetree node was added for this lab (the overlay only has `mbox-consumer` and `&mbox0 { status = "okay"; };`, exactly like Lab 02).
- This header file is **duplicated byte-for-byte** between `src/` and `remote/src/`. Since procpu and appcpu are two completely separate Zephyr applications built for different boards (each with its own `CMakeLists.txt`/`project()`), keeping one small header in sync by hand was simpler than wiring up a shared include path between them. **If you edit this file, edit both copies.**

### 6.2 procpu (`src/main.c`) - the side that keeps sending fast

The main loop checks three things every iteration, all based on `k_uptime_get()` (non-blocking):

1. **GPIO2 heartbeat**: toggles every 500ms (same as Labs 01/02)
2. **Sends every 200ms**: increments `total_sent`, writes it into `SHM_P2A_ADDR`, then rings appcpu's doorbell with `mbox_send_dt(&tx_channel, NULL)` (still a NULL payload - the actual data goes through shared memory, and MBOX only carries the "new data available" signal, which is this lab's core design). Every send is logged with `LOG_INF("sent seq=%u ...")`
3. **Receiving appcpu's replies**: every time appcpu rings this core's doorbell (from ISR context), `atomic_inc(&reply_pending)` just bumps a count. The main loop drains all pending replies with `while (atomic_get(&reply_pending) > 0)`, reading `SHM_A2P_ADDR` and logging `LOG_INF("appcpu reply: drained_count=... last_seen_seq=... backlog=...")`

### 6.3 appcpu (`remote/src/main.c`) - the side that processes deliberately slowly

- **GPIO42 heartbeat**: toggles every 200ms normally (same as Labs 01/02)
- Every time procpu's doorbell rings (ISR callback), `atomic_inc(&pending)` just bumps a pending count
- The main loop pulls **at most one item off the queue every `PROCESS_PERIOD_MS` (350ms)**:
  - Reads whatever `seq` value happens to be in `SHM_P2A_ADDR` **right now** (note: this is NOT necessarily the value that was current at the moment THIS particular doorbell was rung - procpu may have already overwritten it one or more times in the meantime)
  - Writes `{drained_count, last_seen_seq}` into `SHM_A2P_ADDR` and rings procpu's doorbell to reply
  - Shows a quick double-flash (80ms period, 4 edges) on the LED before returning to its normal heartbeat
- Deliberately setting **`PROCESS_PERIOD_MS` (350ms) slower than procpu's `SEND_PERIOD_MS` (200ms)** is the key trick of this lab - it prevents appcpu from ever catching up to procpu, so the phenomenon in section 7 actually becomes observable.

## 7. The race condition: what happens, and why

Since procpu sends one every 200ms while appcpu only processes one every 350ms, the "backlog of un-processed doorbells" (`atomic_t pending`) keeps growing over time. Two different things are worth observing here:

- **`drained_count` (the notification *count* is never lost)**: because `pending` is an `atomic_t` rather than a plain `bool`/`int`, even if the ISR calls `atomic_inc` many times back-to-back, none of those increments ever get silently overwritten. As long as appcpu keeps draining one at a time with `atomic_dec`, no matter how slowly, `drained_count` is guaranteed to eventually catch up to `total_sent` (though always trailing behind - `backlog = total_sent - drained_count` is logged by procpu every time).
- **`last_seen_seq` (but the *value* is lost)**: `SHM_P2A_ADDR` has exactly one slot, so every time procpu writes a new value, the previous one is simply gone. Since appcpu only comes to read it once every 350ms, procpu may have already overwritten that slot several times in between - so `last_seen_seq` does NOT increase 1,2,3,4... in lockstep with `drained_count`; instead it **skips values** (e.g. 1 → 4 → 7 → 9 ...). In other words, appcpu correctly counts "how many times the doorbell rang", but has already lost most of the information about "what value each of those doorbells actually carried".

You'll see roughly this pattern in procpu's serial log (exact numbers vary with timing):

```
[00:00:01.200] sent seq=6 (total_sent=6)
[00:00:01.400] sent seq=7 (total_sent=7)
[00:00:01.410] appcpu reply: drained_count=3 last_seen_seq=7 (procpu total_sent=7, backlog=4)
[00:00:01.600] sent seq=8 (total_sent=8)
```

`drained_count=3` together with `last_seen_seq=7` means appcpu's third processed item did not actually see the (already-gone) values 4, 5, or 6 - it read whatever the latest value, 7, happened to be at that moment. **The notification was correctly counted as the 3rd one processed, but the actual data from 4 and 5 and 6 is gone for good.**

This is the fundamental limitation you inevitably run into when you build IPC by hand with nothing but shared memory and a doorbell, with no framework underneath. `atomic_t` correctly preserves "how many notifications arrived", but it cannot preserve "what data each notification carried" - because there's only one slot. Solving that requires an actual message queue (each message kept in its own slot, e.g. a ring buffer) - which is exactly the problem that `IPC Service` (the `icmsg`/`rpmsg` backends covered in Lab 04/05) already solves for you at the framework level.

## 8. Checklist for correct behavior

- [ ] The LED on GPIO2 keeps blinking at 0.5s intervals (PROCPU heartbeat)
- [ ] The LED on GPIO42 normally blinks at 0.2s intervals, and periodically shows a quick double-flash (two blips at an 80ms interval) whenever APPCPU finishes processing one backlog item
- [ ] The serial terminal shows `03_SHM_RACE_LAB (core0/procpu) starting` and `reusing ipmmem0 at 0x3fce5000, size 1024 bytes (half=512)`
- [ ] `sent seq=N (total_sent=N)` keeps appearing and increasing roughly every 200ms
- [ ] `appcpu reply: drained_count=... last_seen_seq=... backlog=...` appears roughly every 350ms, and **the `backlog` value grows slowly over time**
- [ ] You can directly see in the log that `last_seen_seq` does NOT increase by 1 each time the way `drained_count` does - it **skips values** here and there (the race condition explained in section 7)

## 9. Confirmed on real hardware

All of the checklist items above were confirmed on a real DevKitC-1 board. Here is an excerpt of the actual log, taken about 1 minute 30 seconds in:

```
[00:01:30.226,000] <inf> app_procpu: appcpu reply: drained_count=255 last_seen_seq=445 (procpu total_sent=445, backlog=190)
[00:01:30.398,000] <inf> app_procpu: sent seq=446 (total_sent=446)
[00:01:30.580,000] <inf> app_procpu: appcpu reply: drained_count=256 last_seen_seq=446 (procpu total_sent=446, backlog=190)
[00:01:30.600,000] <inf> app_procpu: sent seq=447 (total_sent=447)
[00:01:30.802,000] <inf> app_procpu: sent seq=448 (total_sent=448)
[00:01:30.933,000] <inf> app_procpu: appcpu reply: drained_count=257 last_seen_seq=448 (procpu total_sent=448, backlog=191)
```

Both effects this doc describes show up exactly as expected:

- **The count is preserved**: `drained_count` increases by exactly 1 each time - 255 → 256 → 257. Proof that `atomic_t` never lost a single notification.
- **The value is lost**: `last_seen_seq` goes 445 → 446 → 448 - **447 is missing entirely**. Right after procpu sent 447, it immediately overwrote the slot with 448 before appcpu ever got around to reading it, so 447 was already gone by the time appcpu came to check.
- **The backlog keeps growing**: `backlog` climbing from 190 to 191 is also visible - this is the expected, intentional result of appcpu's processing rate (350ms) being fundamentally slower than procpu's sending rate (200ms).

## What's next

The limitation confirmed in this lab - that notification counts can be tracked safely, but a single shared slot fundamentally loses data in between - is exactly what Lab 04 (`IPC Service` - `icmsg` backend) picks up next, showing how a standard framework solves it.
