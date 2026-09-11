/*
 * common.h - 04_IPC_SERVICE_ICMSG_LAB
 *
 * The single message type exchanged over the icmsg endpoint in both
 * directions. This file is intentionally duplicated byte-for-byte between
 * src/ and remote/src/ - see shared_mem.h in 03_SHM_RACE_LAB for why
 * (procpu and appcpu are two completely separate Zephyr applications). If
 * you edit this file, edit both copies.
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>

struct app_msg {
	uint32_t seq;
};

#endif /* COMMON_H_ */
