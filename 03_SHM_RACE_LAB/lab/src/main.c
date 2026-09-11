/*
 * main.c - 03_SHM_RACE_LAB (core0 / procpu image)
 *
 * Three independent things happen in this loop:
 *   1. Blinks its own heartbeat LED (GPIO2) every 500ms, same as Labs 01/02.
 *   2. Every SEND_PERIOD_MS, writes an incrementing sequence number into
 *      its half of the reused ipmmem0 block (see shared_mem.h) and rings
 *      appcpu's MBOX doorbell - a pure signal, no payload in the MBOX
 *      message itself (mbox_send_dt with NULL), exactly like Lab 02.
 *   3. Listens for appcpu's own doorbell replies (sent once per backlog
 *      item appcpu manages to process - see remote/src/main.c) and logs
 *      what appcpu actually saw, so "what I sent" vs "what appcpu actually
 *      received" can be directly compared on this core's serial console
 *      alone (appcpu has no console - see 01_HELLO_DUALCORE_LAB's doc).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include "shared_mem.h"

LOG_MODULE_REGISTER(app_procpu, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct mbox_dt_spec tx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
static const struct mbox_dt_spec rx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

#define LED_PERIOD_MS  500
#define SEND_PERIOD_MS 200
#define POLL_PERIOD_MS 10

/*
 * Incremented from ISR context every time appcpu rings this core's
 * doorbell. Using atomic_t (instead of a plain bool/int) is exactly the
 * fix this lab is about: it guarantees that even if several doorbells
 * arrive back-to-back before the main loop gets around to draining them,
 * every single one is still counted - none of them get silently
 * overwritten the way a plain "new_reply = true;" flag could be.
 */
static atomic_t reply_pending = ATOMIC_INIT(0);

static void doorbell_from_appcpu(const struct device *dev, mbox_channel_id_t channel_id,
				  void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);
	ARG_UNUSED(data);

	atomic_inc(&reply_pending);
}

int main(void)
{
	LOG_INF("03_SHM_RACE_LAB (core0/procpu) starting");
	LOG_INF("reusing ipmmem0 at 0x%08x, size %u bytes (half=%u)",
		(unsigned int)SHM_BASE, (unsigned int)SHM_SIZE, (unsigned int)SHM_HALF_SIZE);

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("led gpio not ready");
		return -1;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		LOG_ERR("failed to configure led");
		return -1;
	}

	mbox_register_callback_dt(&rx_channel, doorbell_from_appcpu, NULL);
	mbox_set_enabled_dt(&rx_channel, true);

	bool led_state = false;
	int64_t next_led_toggle = k_uptime_get() + LED_PERIOD_MS;
	int64_t next_send = k_uptime_get() + SEND_PERIOD_MS;
	uint32_t total_sent = 0;

	volatile struct p2a_payload *p2a = (volatile struct p2a_payload *)SHM_P2A_ADDR;
	volatile struct a2p_payload *a2p = (volatile struct a2p_payload *)SHM_A2P_ADDR;

	while (1) {
		int64_t now = k_uptime_get();

		if (now >= next_led_toggle) {
			led_state = !led_state;
			gpio_pin_set_dt(&led, led_state);
			next_led_toggle = now + LED_PERIOD_MS;
		}

		if (now >= next_send) {
			total_sent++;
			p2a->seq = total_sent;

			int ret = mbox_send_dt(&tx_channel, NULL);

			if (ret < 0) {
				LOG_ERR("mbox_send_dt failed (%d)", ret);
			} else {
				LOG_INF("sent seq=%u (total_sent=%u)", total_sent, total_sent);
			}

			next_send = now + SEND_PERIOD_MS;
		}

		while (atomic_get(&reply_pending) > 0) {
			atomic_dec(&reply_pending);

			uint32_t drained_count = a2p->drained_count;
			uint32_t last_seen_seq = a2p->last_seen_seq;

			LOG_INF("appcpu reply: drained_count=%u last_seen_seq=%u "
				"(procpu total_sent=%u, backlog=%u)",
				drained_count, last_seen_seq, total_sent,
				total_sent - drained_count);
		}

		k_msleep(POLL_PERIOD_MS);
	}

	return 0;
}
