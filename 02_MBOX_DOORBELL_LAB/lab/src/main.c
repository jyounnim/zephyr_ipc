/*
 * main.c - 02_MBOX_DOORBELL_LAB (core0 / procpu image)
 *
 * This core does three independent things in one loop:
 *   1. Blinks its own heartbeat LED (GPIO2) every 500ms, same as Lab 01.
 *   2. Polls the onboard BOOT button (sw0 / GPIO0). On each press, it
 *      rings appcpu's doorbell over MBOX - a pure signal, no data at all
 *     ( mbox_send_dt with a NULL msg).
 *   3. Listens for appcpu's own doorbell (appcpu rings it on a timer -
 *      see remote/src/main.c) and logs each one it receives.
 *
 * This is deliberately "signal-only" MBOX usage: nothing but the fact
 * that a doorbell rang is ever communicated. Sending actual data over
 * IPC is a later lab's topic.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_procpu, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static const struct mbox_dt_spec tx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
static const struct mbox_dt_spec rx_channel = MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

#define LED_PERIOD_MS  500
#define POLL_PERIOD_MS 20

static volatile uint32_t doorbell_rx_count;

/* Called (from ISR context) whenever appcpu rings its doorbell. Signal-only
 * doorbell, so `data` carries nothing useful here - just count and log.
 */
static void doorbell_from_appcpu(const struct device *dev, mbox_channel_id_t channel_id,
				  void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);
	ARG_UNUSED(data);

	doorbell_rx_count++;
	LOG_INF("doorbell from appcpu received (count=%u)", doorbell_rx_count);
}

int main(void)
{
	LOG_INF("02_MBOX_DOORBELL_LAB (core0/procpu) starting");

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("led or button gpio not ready");
		return -1;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		LOG_ERR("failed to configure led");
		return -1;
	}

	if (gpio_pin_configure_dt(&button, GPIO_INPUT)) {
		LOG_ERR("failed to configure button");
		return -1;
	}

	mbox_register_callback_dt(&rx_channel, doorbell_from_appcpu, NULL);
	mbox_set_enabled_dt(&rx_channel, true);

	bool led_state = false;
	bool button_prev = false;
	int64_t next_led_toggle = k_uptime_get() + LED_PERIOD_MS;

	while (1) {
		bool button_now = gpio_pin_get_dt(&button) > 0;

		if (button_now && !button_prev) {
			LOG_INF("button pressed - ringing appcpu's doorbell");
			int ret = mbox_send_dt(&tx_channel, NULL);

			if (ret < 0) {
				LOG_ERR("mbox_send_dt failed (%d)", ret);
			}
		}
		button_prev = button_now;

		int64_t now = k_uptime_get();

		if (now >= next_led_toggle) {
			led_state = !led_state;
			gpio_pin_set_dt(&led, led_state);
			LOG_INF("procpu heartbeat - LED %s", led_state ? "ON" : "OFF");
			next_led_toggle = now + LED_PERIOD_MS;
		}

		k_msleep(POLL_PERIOD_MS);
	}

	return 0;
}
