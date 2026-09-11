/*
 * main.c - 04_IPC_SERVICE_ICMSG_LAB (core1 / appcpu image)
 *
 * No console here (see 01_HELLO_DUALCORE_LAB's doc for why). This core:
 *   - Blinks its heartbeat LED (GPIO42) every 200ms, same as every lab so far.
 *   - Immediately echoes every message it receives straight back to procpu
 *     (same seq value), called directly from inside the IPC Service
 *     "received" callback itself.
 *
 * Calling ipc_service_send() from inside the received callback is safe
 * here: with multithreading enabled (the default, and what every lab in
 * this series uses), Zephyr's icmsg backend invokes this callback from its
 * own workqueue thread - not from raw mbox ISR context - and holds no lock
 * while calling it, so sending straight back out from inside it cannot
 * deadlock (confirmed by reading subsys/ipc/ipc_service/lib/icmsg.c).
 *
 * The LED double-flash on each echo is a cosmetic-only signal, decoupled
 * from the actual protocol - if two echoes happen to land in the same
 * flash window, the flash count may look coalesced, but that says nothing
 * about whether any *message* was lost (see the doc's procpu-side log
 * comparison for the real proof of that).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>

#include "common.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static struct ipc_ept ep;

#define BLINK_PERIOD_MS       200
#define REACT_FLASH_PERIOD_MS 80
#define REACT_FLASH_EDGES     4 /* 4 edges = 2 full on/off flashes */

K_SEM_DEFINE(bound_sem, 0, 1);

static volatile int reaction_edges_remaining;

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);

	k_sem_give(&bound_sem);
}

static void ep_recv(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len != sizeof(struct app_msg)) {
		return;
	}

	const struct app_msg *msg = data;
	struct app_msg reply = { .seq = msg->seq };

	ipc_service_send(&ep, &reply, sizeof(reply));
	reaction_edges_remaining = REACT_FLASH_EDGES;
}

static struct ipc_ept_cfg ep_cfg = {
	.cb = {
		.bound = ep_bound,
		.received = ep_recv,
	},
};

int main(void)
{
	const struct device *ipc0_instance;
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		return -1;
	}

	ipc0_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

	ret = ipc_service_open_instance(ipc0_instance);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		return ret;
	}

	k_sem_take(&bound_sem, K_FOREVER);

	bool led_state = false;
	int64_t next_toggle = k_uptime_get() + BLINK_PERIOD_MS;

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

		k_msleep(10);
	}

	return 0;
}
