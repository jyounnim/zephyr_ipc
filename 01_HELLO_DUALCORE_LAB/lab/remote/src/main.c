/*
 * main.c - 01_HELLO_DUALCORE_LAB (core1 / appcpu image)
 *
 * This core does exactly one job: blink its own LED, on its own clock,
 * with no coordination with core0 at all. There is no IPC in this lab -
 * both images just happen to be flashed onto the same chip and run at
 * the same time. Lab 02 is where the two cores start actually talking.
 *
 * Deliberately no logging/console output here: Zephyr's UART console
 * driver is not yet available on the ESP32-S3 appcpu image (see this
 * board's Zephyr documentation and this lab's KR/EN doc for details), so
 * a LOG_INF() call here would have nowhere to go. The LED is the only
 * proof-of-life this core has - which is the whole point of this lab.
 *
 * Blink period is intentionally different from core0's (200ms vs 500ms)
 * so the two LEDs are visually distinguishable as running independently,
 * not in lock-step.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#define BLINK_PERIOD_MS 200

int main(void)
{
	if (!gpio_is_ready_dt(&led)) {
		/* No console to log to - nothing more useful to do than
		 * return and let the board stay silent/idle, which itself
		 * is a symptom to look for if the LED never lights up.
		 */
		return -1;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (ret) {
		return -1;
	}

	bool led_state = false;

	while (1) {
		led_state = !led_state;
		gpio_pin_set_dt(&led, led_state);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
