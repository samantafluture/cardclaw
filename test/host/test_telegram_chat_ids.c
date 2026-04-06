/*
 * Host tests for Telegram chat ID allowlist parsing helpers.
 */

#include <stdio.h>

#include "telegram_chat_ids.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(parse_single_id)
{
    int64_t ids[TELEGRAM_MAX_ALLOWED_CHAT_IDS] = {0};
    size_t count = 0;

    ASSERT(telegram_chat_ids_parse("7585013353", ids, TELEGRAM_MAX_ALLOWED_CHAT_IDS, &count));
    ASSERT(count == 1);
    ASSERT(ids[0] == 7585013353LL);
    return 0;
}

TEST(parse_csv_with_spaces_and_dedupes)
{
    int64_t ids[TELEGRAM_MAX_ALLOWED_CHAT_IDS] = {0};
    size_t count = 0;

    ASSERT(telegram_chat_ids_parse(" 7585013353, -100222333444 ,7585013353 ",
                                   ids, TELEGRAM_MAX_ALLOWED_CHAT_IDS, &count));
    ASSERT(count == 2);
    ASSERT(ids[0] == 7585013353LL);
    ASSERT(ids[1] == -100222333444LL);
    return 0;
}

TEST(rejects_invalid_input)
{
    int64_t ids[TELEGRAM_MAX_ALLOWED_CHAT_IDS] = {0};
    size_t count = 0;

    ASSERT(!telegram_chat_ids_parse("", ids, TELEGRAM_MAX_ALLOWED_CHAT_IDS, &count));
    ASSERT(!telegram_chat_ids_parse("abc", ids, TELEGRAM_MAX_ALLOWED_CHAT_IDS, &count));
    ASSERT(!telegram_chat_ids_parse("0", ids, TELEGRAM_MAX_ALLOWED_CHAT_IDS, &count));
    ASSERT(!telegram_chat_ids_parse("1,2,3,4,5", ids, 4, &count));
    return 0;
}

TEST(contains_helper)
{
    int64_t ids[TELEGRAM_MAX_ALLOWED_CHAT_IDS] = {7585013353LL, -100222333444LL};
    ASSERT(telegram_chat_ids_contains(ids, 2, 7585013353LL));
    ASSERT(telegram_chat_ids_contains(ids, 2, -100222333444LL));
    ASSERT(!telegram_chat_ids_contains(ids, 2, 11111111LL));
    return 0;
}

TEST(resolve_target_fails_closed_for_unauthorized_id)
{
    int64_t ids[TELEGRAM_MAX_ALLOWED_CHAT_IDS] = {7585013353LL, -100222333444LL};
    int64_t primary = 7585013353LL;

    ASSERT(telegram_chat_ids_resolve_target(ids, 2, primary, 0) == primary);
    ASSERT(telegram_chat_ids_resolve_target(ids, 2, primary, -100222333444LL) == -100222333444LL);
    ASSERT(telegram_chat_ids_resolve_target(ids, 2, primary, 99999999LL) == 0);
    return 0;
}

int test_telegram_chat_ids_all(void)
{
    int failures = 0;

    printf("\nTelegram Chat ID Tests:\n");

    printf("  parse_single_id... ");
    if (test_parse_single_id() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  parse_csv_with_spaces_and_dedupes... ");
    if (test_parse_csv_with_spaces_and_dedupes() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  rejects_invalid_input... ");
    if (test_rejects_invalid_input() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  contains_helper... ");
    if (test_contains_helper() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  resolve_target_fails_closed_for_unauthorized_id... ");
    if (test_resolve_target_fails_closed_for_unauthorized_id() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    return failures;
}
