#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(l5task1, LOG_LEVEL_DBG);

#define STACK_SIZE             2048
#define SENSOR_COUNT           40
#define SENSOR_PERIOD_MS       100
#define CONSUMER_STUCK_MS      3000
#define CONSUMER_WDT_TIMEOUT_MS 500
#define HEALTH_PERIOD_MS       100
#define QUEUE_CAPACITY         8
#define QUEUE_WARNING_PERCENT  75

struct sensor_data {
	int32_t temperature_mc;
	uint32_t timestamp_ms;
	uint8_t seq;
};

K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_data), QUEUE_CAPACITY, 4);
K_SEM_DEFINE(task_wdt_ready, 0, 1);

static int consumer_wdt_id = -1;

static void display_listener_cb(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
		 NULL, NULL,
		 ZBUS_OBSERVERS(display_lis, logger_sub),
		 ZBUS_MSG_INIT(.temperature_mc = 0,
			       .timestamp_ms = 0,
			       .seq = 0));

static void task_wdt_callback(int channel_id, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_ERR("[WATCHDOG] consumer timed out, channel=%d", channel_id);
}

/*
 * The listener executes in the publisher context. It only performs a
 * non-blocking queue operation, leaving processing to the consumer thread.
 */
static void display_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_data *msg =
		(const struct sensor_data *)zbus_chan_const_msg(chan);
	int ret;

	LOG_INF("[DISPLAY-LIS] thread=%s seq=%u temp=%d mC",
		k_thread_name_get(k_current_get()),
		msg->seq, msg->temperature_mc);

	ret = k_msgq_put(&sensor_queue, msg, K_NO_WAIT);
	if (ret != 0) {
		LOG_WRN("[PIPELINE] queue full, dropped seq=%u ret=%d",
			msg->seq, ret);
	}
}

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "sensor");

	for (uint8_t seq = 0; seq < SENSOR_COUNT; seq++) {
		struct sensor_data data = {
			.temperature_mc = 24000 + (seq * 350),
			.timestamp_ms = k_uptime_get_32(),
			.seq = seq,
		};

		LOG_INF("[SENSOR] publish seq=%u temp=%d mC",
			data.seq, data.temperature_mc);

		int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
		if (ret != 0) {
			LOG_WRN("[SENSOR] publish failed ret=%d", ret);
		}

		k_msleep(SENSOR_PERIOD_MS);
	}

	LOG_INF("[SENSOR] done");
}

static void consumer_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "consumer");

	k_sem_take(&task_wdt_ready, K_FOREVER);

	consumer_wdt_id = task_wdt_add(CONSUMER_WDT_TIMEOUT_MS,
				       task_wdt_callback,
				       (void *)k_current_get());
	if (consumer_wdt_id < 0) {
		LOG_ERR("[CONSUMER] watchdog registration failed: %d",
			consumer_wdt_id);
		return;
	}

	while (true) {
		struct sensor_data msg;
		int ret = k_msgq_get(&sensor_queue, &msg, K_FOREVER);

		if (ret != 0) {
			LOG_ERR("[CONSUMER] queue receive failed: %d", ret);
			continue;
		}

		LOG_INF("[CONSUMER] seq=%u temp=%d mC latency=%ums",
			msg.seq, msg.temperature_mc,
			k_uptime_get_32() - msg.timestamp_ms);

		task_wdt_feed(consumer_wdt_id);

		LOG_WRN("[CONSUMER] simulating stuck consumer for %d ms",
			CONSUMER_STUCK_MS);
		k_msleep(CONSUMER_STUCK_MS);
	}
}

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "zbus-logger");

	while (true) {
		const struct zbus_channel *chan;
		struct sensor_data msg;
		int ret = zbus_sub_wait_msg(&logger_sub, &chan, &msg, K_FOREVER);

		if (ret != 0) {
			LOG_WRN("[ZBUS-LOGGER] receive failed: %d", ret);
			continue;
		}

		LOG_DBG("[ZBUS-LOGGER] seq=%u temp=%d mC",
			msg.seq, msg.temperature_mc);
	}
}

static void health_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "health");

	while (true) {
		uint32_t used = k_msgq_num_used_get(&sensor_queue);
		uint32_t percent = (used * 100U) / QUEUE_CAPACITY;

		if ((used * 100U) >=
		    (QUEUE_CAPACITY * QUEUE_WARNING_PERCENT)) {
			LOG_WRN("[HEALTH] queue fill level %u/%u (%u%%)",
				used, QUEUE_CAPACITY, percent);
		} else {
			LOG_DBG("[HEALTH] queue fill level %u/%u (%u%%)",
				used, QUEUE_CAPACITY, percent);
		}

		k_msleep(HEALTH_PERIOD_MS);
	}
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
		NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(consumer_thread, STACK_SIZE, consumer_thread_fn,
		NULL, NULL, NULL, 6, 0, 0);

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
		NULL, NULL, NULL, 7, 0, 0);

K_THREAD_DEFINE(health_thread, STACK_SIZE, health_thread_fn,
		NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int ret = task_wdt_init(NULL);

	if (ret != 0) {
		LOG_ERR("task_wdt_init failed: %d", ret);
		return 0;
	}

	k_sem_give(&task_wdt_ready);

	LOG_INF("=== L4 task: zbus producer/consumer pipeline ===");
	LOG_INF("sensor publishes every %d ms", SENSOR_PERIOD_MS);
	LOG_INF("queue capacity=%d, warning threshold=%d%%",
		QUEUE_CAPACITY, QUEUE_WARNING_PERCENT);

	return 0;
}
