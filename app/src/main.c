#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE    1024
#define SENSOR_MS     100    /* sensor fires every 100ms */
#define POLL_MS       10     /* polling consumer checks every 10ms */
#define EVENT_COUNT   10     /* total sensor events to produce */

/* ================================================================
 * STARTER CODE -- inefficient polling version
 * Run this first, then replace with workqueue in Task 2.
 * ================================================================ */

/* Shared flag between sensor_sim and polling_thread */
//static volatile bool sensor_flag;

/* Statistics */
static int total_events;
//static int total_wakeups;
static int total_processed;


/* ------------------------------------------------------------------ */
/*  polling_thread - checks flag every 10ms                          */
/*                                                                     */
/*  TASK 2: Replace this entire function + thread with a k_work       */
/*  handler. The handler body is the same as what's inside the        */
/*  if (sensor_flag) block below.                                      */
/* ------------------------------------------------------------------ */

// static void polling_fn(void *p1, void *p2, void *p3)
// {
//     ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

//     while (total_processed < EVENT_COUNT) {
//         k_msleep(POLL_MS);
//         total_wakeups++;

//         if (sensor_flag) {
//             sensor_flag = false;
//             total_processed++;

//             /*
//              * This is the "real work". In Task 2 this goes into
//              * the k_work handler body.
//              */
//             LOG_INF("[CONSUMER] processed event %d  wakeups_so_far=%d  tick=%u",
//                     total_processed, total_wakeups,
//                     k_uptime_get_32());
//         }
//     }

    /* Summary after all events processed */
//     LOG_INF("\n");
//     LOG_INF("[SUMMARY] events=%d  total_wakeups=%d  wasted=%d",
//             total_processed,
//             total_wakeups,
//             total_wakeups - total_processed);
//     LOG_INF("[SUMMARY] wasted wakeups = %d%% of all wakeups",
//             (total_wakeups - total_processed) * 100 /
//             total_wakeups);
// }

/* ------------------------------------------------------------------ */
/*  Threads                                                             */
/*                                                                     */
/*  TASK 2: Remove the polling_thread define. Add a K_WORK_DEFINE     */
/*  for your handler here instead.                                     */
/* ------------------------------------------------------------------ */


//K_THREAD_DEFINE(polling_thread, STACK_SIZE, polling_fn,    NULL, NULL, NULL, 5, 0, 0);

/* ================================================================
 * TASK 2 PLACEHOLDER - implement your solution here
 *
 * Uncomment and fill in:
 *
 * static void sensor_handler(struct k_work *work)
 * {
 *     ARG_UNUSED(work);
 *     total_processed++;
 *     LOG_INF("[HANDLER] processed event %d  tick=%u",
 *             total_processed, k_uptime_get_32());
 * }
 *
 * K_WORK_DEFINE(sensor_work, sensor_handler);
 *
 * BONUS PLACEHOLDER - for debounce:
 *
 * K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);
 * In sensor_sim: k_work_reschedule(&debounce_work, K_MSEC(30));
 * ================================================================ */

static void sensor_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    total_processed++;
    LOG_INF("[HANDLER] processed event %d  tick=%u",
            total_processed, k_uptime_get_32());
}
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);

 /* ------------------------------------------------------------------ */
/*  sensor_sim - fires EVENT_COUNT events, 100ms apart               */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
        k_msleep(SENSOR_MS);

        total_events++;
        LOG_INF("[SENSOR] event %d  tick=%u", i, k_uptime_get_32());

        /*
         * STARTER: set a flag for the polling thread.
         *
         * TASK 2: Replace these two lines with:
         *   int ret = k_work_submit(&sensor_work);
         *   if (ret < 0) { LOG_ERR("submit failed: %d", ret); }
         *
         * Remove sensor_flag entirely once you do that.
         */
        //sensor_flag = true;
        //int ret = k_work_submit(&sensor_work);
        //if (ret < 0) { LOG_ERR("submit failed: %d", ret); }
        for (int j = 0; j < 5; j++) {
            k_msleep(4);
            int ret = k_work_reschedule(&debounce_work, K_MSEC(30));
            if (ret < 0) { LOG_ERR("reschedule failed: %d", ret); }
        }
        /*
         * BONUS: Replace the single k_msleep(SENSOR_MS) above with
         * a burst of 5 rapid events, then use k_work_reschedule in
         * the handler to collapse them to one execution.
         */
    }

    LOG_INF("[SENSOR] all events produced");
}
K_THREAD_DEFINE(sensor_thread,  STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("=== L3 Homework: Polling to Workqueue ===");
    LOG_INF("Starter: polling every %dms, sensor fires every %dms",
            POLL_MS, SENSOR_MS);
    LOG_INF("Expected wasted wakeups: ~%d per event",
            (SENSOR_MS / POLL_MS) - 1);
    LOG_INF("Run this, count wakeups, then convert to workqueue.");

    /* Wait long enough for all events to complete */
    k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

    return 0;
}