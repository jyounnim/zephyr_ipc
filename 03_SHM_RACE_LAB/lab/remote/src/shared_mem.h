/*
 * shared_mem.h - 03_SHM_RACE_LAB
 *
 * Describes the raw memory layout this lab uses inside ipmmem0, the 1KB
 * SRAM block (physical address 0x3fce5000) that the SoC devicetree already
 * reserves as &ipm0/&mbox0's own private message-copy scratch buffer (see
 * the "shared-memory"/"shared-memory-size" properties on those nodes in
 * Zephyr's esp32s3_common.dtsi). This lab reuses that exact block instead
 * of asking for a brand-new reserved-memory region, because every
 * mbox_send_dt() call this lab makes always passes a NULL payload (a pure
 * signal, same as 02_MBOX_DOORBELL_LAB) - so the mbox0 driver's own
 * memcpy into this buffer (drivers/mbox/mbox_esp32.c) is never triggered,
 * and the whole block is free for this lab's own use. See the doc for the
 * full reasoning.
 *
 * This file is intentionally duplicated byte-for-byte between src/ and
 * remote/src/ rather than shared through a common include path, because
 * procpu and appcpu are two completely separate Zephyr applications
 * (separate CMakeLists.txt / project() calls) built for two different
 * boards - keeping one small header in sync by hand is simpler here than
 * wiring up a shared include directory between them. If you edit this
 * file, edit both copies.
 */

#ifndef SHARED_MEM_H_
#define SHARED_MEM_H_

#include <zephyr/devicetree.h>
#include <stdint.h>

/* Base address/size of the reused ipmmem0 block, read directly from the
 * devicetree node - no application-defined devicetree node is needed for
 * this, since ipmmem0 is already declared (unconditionally) at the SoC
 * level and its "reg" property is readable via its label regardless of
 * whether &ipm0/&mbox0 are enabled. */
#define SHM_BASE (DT_REG_ADDR(DT_NODELABEL(ipmmem0)))
#define SHM_SIZE (DT_REG_SIZE(DT_NODELABEL(ipmmem0)))

/* mbox0's own driver splits this same 1KB block into two halves, one per
 * direction (see pro_cpu_shm/app_cpu_shm in drivers/mbox/mbox_esp32.c).
 * We mirror that same split for our own traffic, purely for symmetry. */
#define SHM_HALF_SIZE (SHM_SIZE / 2)

/* procpu writes here, appcpu reads (procpu -> appcpu) */
#define SHM_P2A_ADDR (SHM_BASE)
/* appcpu writes here, procpu reads (appcpu -> procpu) */
#define SHM_A2P_ADDR (SHM_BASE + SHM_HALF_SIZE)

/* procpu -> appcpu: one monotonically increasing sequence number per send */
struct p2a_payload {
	uint32_t seq;
};

/* appcpu -> procpu: what appcpu actually managed to process */
struct a2p_payload {
	uint32_t drained_count; /* total backlog items processed so far */
	uint32_t last_seen_seq; /* whatever seq value was in SHM_P2A_ADDR at
				  * the moment this item was processed - NOT
				  * guaranteed to include every value procpu
				  * ever wrote (see the doc)
				  */
};

#endif /* SHARED_MEM_H_ */
