/*
 * main.c - 01_HELLO_DUALCORE_LAB (core0 / procpu image)
 *
 * Purpose of this lab: nothing more than proving that two independent
 * Zephyr images can run on ESP32-S3's two cores at the same time, built
 * and flashed together with a single `west build --sysbuild` /
 * `west flash`. There is NO inter-processor communication in this lab -
 * that starts in Lab 02. This core and remote/src/main.c (core1) never
 * talk to each other; they just happen to blink at different rates so
 * it's visually obvious on the breadboard that both are alive and that
 * neither is waiting on the other.
 *
 * core0 (procpu) has full UART console support, so it proves itself
 * alive two ways: this LED, and a logged heartbeat line every 500ms.
 * Compare this with core1, which has only the LED - see the appcpu
 * main.c and the doc for why.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_procpu, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#define BLINK_PERIOD_MS 500

int main(void)
{
	LOG_INF("01_HELLO_DUALCORE_LAB (core0/procpu) starting");

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready - check overlay/wiring");
		return -1;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (ret) {
		LOG_ERR("gpio_pin_configure_dt failed: %d", ret);
		return -1;
	}

	bool led_state = false;

	while (1) {
		led_state = !led_state;
		gpio_pin_set_dt(&led, led_state);
		LOG_INF("procpu heartbeat - LED %s", led_state ? "ON" : "OFF");
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
