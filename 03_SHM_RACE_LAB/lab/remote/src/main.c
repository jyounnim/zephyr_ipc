/*
 * main.c - 03_SHM_RACE_LAB (core1 / appcpu image)
 *
 * No console here (see 01_HELLO_DUALCORE_LAB's doc for why) - this core's
 * only visible output is its heartbeat LED (GPIO42):
 *
 *   - Normally blinks at a steady 200ms rate, same as Labs 01/02.
 *   - Every time it finishes "processing" one backlog item (see below), it
 *     does a quick double-flash before returning to its normal blink.
 *
 * The actual lesson of this lab happens where you can't see it directly on
 * this core (no console) but CAN see it in procpu's serial log: this core
 * deliberately processes backlog items more slowly (PROCESS_PERIOD_MS)
 * than procpu sends them (SEND_PERIOD_MS in the procpu image), so a
 * backlog builds up. An atomic pending-counter (see "pending" below) makes
 * sure every single doorbell procpu rings is still *counted* even while
 * this core is busy - but because procpu keeps overwriting the SAME
 * shared-memory slot with newer values the whole time, by the time this
 * core gets around to reading it, several intermediate values are already
 * gone for good. That gap is the point: counting notifications correctly
 * (atomic_t) is not the same problem as not losing the DATA that came with
 * each one - see the doc.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/atomic.h>

#include "shared_mem.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct mbox_dt_spec tx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
static const struct mbox_dt_spec rx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

#define BLINK_PERIOD_MS       200
#define REACT_FLASH_PERIOD_MS 80
#define REACT_FLASH_EDGES     4 /* 4 edges = 2 full on/off flashes */

/* Deliberately slower than procpu's SEND_PERIOD_MS (200ms) - this gap is
 * what causes the backlog / lost-values race this lab demonstrates.
 */
#define PROCESS_PERIOD_MS 350
#define POLL_PERIOD_MS    10

/*
 * Incremented from ISR context on every doorbell from procpu. See the
 * procpu image's main.c for why atomic_t (not a plain bool/int) matters
 * here: it guarantees this core eventually finds out about and processes
 * every single doorbell, even ones that arrive while it is still busy
 * with an earlier one.
 */
static atomic_t pending = ATOMIC_INIT(0);

static void doorbell_from_procpu(const struct device *dev, mbox_channel_id_t channel_id,
				  void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);
	ARG_UNUSED(data);

	atomic_inc(&pending);
}

int main(void)
{
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		return -1;
	}

	mbox_register_callback_dt(&rx_channel, doorbell_from_procpu, NULL);
	mbox_set_enabled_dt(&rx_channel, true);

	bool led_state = false;
	int64_t next_toggle = k_uptime_get() + BLINK_PERIOD_MS;
	int64_t next_process = 0; /* ready to process immediately */
	int reaction_edges_remaining = 0;
	uint32_t drained_count = 0;

	volatile struct p2a_payload *p2a = (volatile struct p2a_payload *)SHM_P2A_ADDR;
	volatile struct a2p_payload *a2p = (volatile struct a2p_payload *)SHM_A2P_ADDR;

	while (1) {
		int64_t now = k_uptime_get();

		/*
		 * Process at most one backlog item per PROCESS_PERIOD_MS,
		 * without ever blocking this loop - the LED heartbeat below
		 * keeps running on its own independent schedule regardless
		 * of backlog size.
		 */
		if (now >= next_process && atomic_get(&pending) > 0) {
			atomic_dec(&pending);
			drained_count++;

			/*
			 * Read whatever value happens to be in shared memory
			 * right now - NOT necessarily the value that was
			 * current when THIS particular doorbell was rung,
			 * since procpu may have already overwritten it one or
			 * more times since then.
			 */
			uint32_t seen_seq = p2a->seq;

			a2p->drained_count = drained_count;
			a2p->last_seen_seq = seen_seq;
			mbox_send_dt(&tx_channel, NULL);

			reaction_edges_remaining = REACT_FLASH_EDGES;
			next_process = now + PROCESS_PERIOD_MS;
		}

		if (reaction_edges_remaining > 0) {
			if (now >= next_toggle) {
				led_state = !led_state;
				gpio_pin_set_dt(&led, led_state);
				reaction_edges_remaining--;
				next_toggle = now + REACT_FLASH_PERIOD_MS;
			}
		} else if (now >= next_toggle) {
			led_state = !led_state;
			gpio_pin_set_dt(&led, led_state);
			next_toggle = now + BLINK_PERIOD_MS;
		}

		k_msleep(POLL_PERIOD_MS);
	}

	return 0;
}
