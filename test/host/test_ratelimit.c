/*
 * Host tests for real ratelimit.c runtime behavior.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mock_memory.h"
#include "memory.h"
#include "nvs_keys.h"
#include "ratelimit.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static void seed_current_day_window(void)
{
    time_t now;
    struct tm timeinfo;
    char buf[16];

    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(buf, sizeof(buf), "%d", timeinfo.tm_yday);
    mock_memory_set_kv(NVS_KEY_RL_DAY, buf);
    snprintf(buf, sizeof(buf), "%d", timeinfo.tm_year);
    mock_memory_set_kv(NVS_KEY_RL_YEAR, buf);
    mock_memory_set_kv(NVS_KEY_RL_DAILY, "0");
}

TEST(persist_failures_are_counted_and_retried)
{
    char daily[16] = {0};

    mock_memory_reset();
    seed_current_day_window();
    ratelimit_init();
    ASSERT(ratelimit_test_get_persist_failure_count() == 0);

    mock_memory_fail_next_set(ESP_FAIL);
    ratelimit_record_request();
    ASSERT(ratelimit_test_get_persist_failure_count() == 1);

    ratelimit_record_request();
    ASSERT(memory_get(NVS_KEY_RL_DAILY, daily, sizeof(daily)));
    ASSERT(strcmp(daily, "2") == 0);
    return 0;
}

int test_ratelimit_all(void)
{
    int failures = 0;

    printf("\nRate Limit Tests:\n");

    printf("  persist_failures_are_counted_and_retried... ");
    if (test_persist_failures_are_counted_and_retried() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    return failures;
}
