/*
 * main.c - 04_IPC_SERVICE_ICMSG_LAB (core0 / procpu image)
 *
 * Reimplements 03_SHM_RACE_LAB's experiment - keep sending an incrementing
 * sequence number, have appcpu echo it straight back - but this time
 * through Zephyr's IPC Service framework (icmsg backend) instead of raw
 * MBOX + a hand-rolled shared struct. See the doc for why this fixes the
 * data-loss problem Lab 03 deliberately exposed: icmsg's shared memory is
 * a real queued ring buffer (one slot per message), not a single
 * overwritable slot, so no value in between two reads is ever silently
 * lost the way it was in Lab 03.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>

#include "common.h"

LOG_MODULE_REGISTER(app_procpu, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#define LED_PERIOD_MS  500
#define SEND_PERIOD_MS 200

K_SEM_DEFINE(bound_sem, 0, 1);

static uint32_t total_sent;
static uint32_t total_received;

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);

	LOG_INF("endpoint bound");
	k_sem_give(&bound_sem);
}

static void ep_recv(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len != sizeof(struct app_msg)) {
		LOG_ERR("unexpected message length %zu", len);
		return;
	}

	const struct app_msg *msg = data;

	total_received++;
	LOG_INF("recv reply seq=%u (total_received=%u, total_sent=%u)",
		msg->seq, total_received, total_sent);
}

static void ep_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);

	LOG_ERR("icmsg error: %s", message);
}

static struct ipc_ept_cfg ep_cfg = {
	.cb = {
		.bound = ep_bound,
		.received = ep_recv,
		.error = ep_error,
	},
};

int main(void)
{
	const struct device *ipc0_instance;
	struct ipc_ept ep;
	int ret;

	LOG_INF("04_IPC_SERVICE_ICMSG_LAB (core0/procpu) starting");

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("led gpio not ready");
		return -1;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		LOG_ERR("failed to configure led");
		return -1;
	}

	ipc0_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

	ret = ipc_service_open_instance(ipc0_instance);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("ipc_service_open_instance() failed (%d)", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint() failed (%d)", ret);
		return ret;
	}

	LOG_INF("waiting for endpoint to bind...");
	k_sem_take(&bound_sem, K_FOREVER);

	bool led_state = false;
	int64_t next_led_toggle = k_uptime_get() + LED_PERIOD_MS;
	int64_t next_send = k_uptime_get() + SEND_PERIOD_MS;
	uint32_t pending_seq = 0; /* seq value awaiting a successful send, 0 = none pending */

	while (1) {
		int64_t now = k_uptime_get();

		if (now >= next_led_toggle) {
			led_state = !led_state;
			gpio_pin_set_dt(&led, led_state);
			next_led_toggle = now + LED_PERIOD_MS;
		}

		if (now >= next_send) {
			uint32_t seq = pending_seq ? pending_seq : (total_sent + 1);
			struct app_msg msg = { .seq = seq };

			ret = ipc_service_send(&ep, &msg, sizeof(msg));
			if (ret == -ENOMEM) {
				/*
				 * icmsg's ring buffer is momentarily full - back off
				 * and retry the SAME seq value next tick instead of
				 * moving on to the next one, so we never silently
				 * skip a value the way Lab 03's single shared slot
				 * could.
				 */
				LOG_WRN("send buffer full, retrying seq=%u", seq);
				pending_seq = seq;
			} else if (ret < 0) {
				LOG_ERR("ipc_service_send() failed (%d)", ret);
				pending_seq = seq;
			} else {
				total_sent = seq;
				pending_seq = 0;
				LOG_INF("sent seq=%u (total_sent=%u)", seq, total_sent);
			}

			next_send = now + SEND_PERIOD_MS;
		}

		k_msleep(10);
	}

	return 0;
}
