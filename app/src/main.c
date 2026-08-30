#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define PRIO 5
#define INCREMENTS 1000000

static K_MUTEX_DEFINE(counter_mutex);
static volatile int32_t counter = 0;
static struct k_sem done_sem;

void t_fn(void *p1, void *p2, void *p3)
{
    const char *name = k_thread_name_get(k_current_get());
    for (int i = 0; i < INCREMENTS; i++)
    {
        k_mutex_lock(&counter_mutex, K_FOREVER);
        counter++;
        k_mutex_unlock(&counter_mutex);
    }
    LOG_INF("[%s] finished", name);
    k_sem_give(&done_sem);
}

K_THREAD_DEFINE(thread_a, STACK_SIZE, t_fn,
                NULL, NULL, NULL, PRIO, 0, 0);
K_THREAD_DEFINE(thread_b, STACK_SIZE, t_fn,
                NULL, NULL, NULL, PRIO, 0, 0);

int main(void)
{
    k_sem_init(&done_sem, 0, 2);

    LOG_INF("Expected final value: %d", INCREMENTS * 2);

    /* Wait for both workers to complete */
    k_sem_take(&done_sem, K_FOREVER);
    k_sem_take(&done_sem, K_FOREVER);

    LOG_INF("Actual  final value: %u", counter);

    return 0;
}
