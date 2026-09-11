/*
 * main.c - 02_MBOX_DOORBELL_LAB (core1 / appcpu image)
 *
 * No console here (see 01_HELLO_DUALCORE_LAB's doc for why) - this core
 * proves what it's doing entirely through its heartbeat LED (GPIO42):
 *
 *   - Normally blinks at a steady 200ms rate, same as Lab 01.
 *   - Whenever procpu's button rings this core's doorbell over MBOX, the
 *     LED breaks into a fast triple-flash (6 edges at 80ms) before
 *     returning to its normal steady blink - a visible, unmistakable
 *     reaction with no console needed.
 *   - Independently of all that, this core rings procpu's doorbell on
 *     its own timer every 3 seconds, with no payload - procpu logs each
 *     one it receives. This is the "other direction" of this lab's
 *     bidirectional doorbell.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mbox.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct mbox_dt_spec tx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
static const struct mbox_dt_spec rx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

#define BLINK_PERIOD_MS         200
#define REACT_FLASH_PERIOD_MS   80
#define REACT_FLASH_EDGES       6 /* 6 edges = 3 full on/off flashes */
#define DOORBELL_SEND_PERIOD_MS 3000
#define POLL_PERIOD_MS          10

static volatile int reaction_edges_remaining;

/* Called (from ISR context) whenever procpu's button rings this core's
 * doorbell. Signal-only - just arm the visible LED reaction.
 */
static void doorbell_from_procpu(const struct device *dev, mbox_channel_id_t channel_id,
				  void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);
	ARG_UNUSED(data);

	reaction_edges_remaining = REACT_FLASH_EDGES;
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
	int64_t next_doorbell = k_uptime_get() + DOORBELL_SEND_PERIOD_MS;

	while (1) {
		int64_t now = k_uptime_get();

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

		if (now >= next_doorbell) {
			mbox_send_dt(&tx_channel, NULL);
			next_doorbell = now + DOORBELL_SEND_PERIOD_MS;
		}

		k_msleep(POLL_PERIOD_MS);
	}

	return 0;
}
