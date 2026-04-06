#include "telegram_chat_ids.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool parse_chat_id_token(const char *start, size_t len, int64_t *out_id)
{
    char token[32];
    char *endptr = NULL;
    long long parsed;

    if (!start || !out_id || len == 0 || len >= sizeof(token)) {
        return false;
    }

    memcpy(token, start, len);
    token[len] = '\0';

    errno = 0;
    parsed = strtoll(token, &endptr, 10);
    if (errno != 0 || !endptr || endptr == token || *endptr != '\0' || parsed == 0) {
        return false;
    }

    *out_id = (int64_t)parsed;
    return true;
}

bool telegram_chat_ids_parse(const char *input,
                             int64_t *out_ids,
                             size_t max_ids,
                             size_t *out_count)
{
    const char *cursor = input;
    size_t count = 0;

    if (!input || !out_ids || !out_count || max_ids == 0) {
        return false;
    }

    *out_count = 0;

    while (1) {
        const char *token_start;
        const char *token_end;
        int64_t parsed_id = 0;
        bool duplicate = false;

        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }

        token_start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        token_end = cursor;

        while (token_end > token_start && isspace((unsigned char)token_end[-1])) {
            token_end--;
        }

        if (token_end > token_start) {
            if (!parse_chat_id_token(token_start, (size_t)(token_end - token_start), &parsed_id)) {
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                if (out_ids[i] == parsed_id) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                if (count >= max_ids) {
                    return false;
                }
                out_ids[count++] = parsed_id;
            }
        }

        if (*cursor == '\0') {
            break;
        }
        cursor++;
    }

    if (count == 0) {
        return false;
    }

    *out_count = count;
    return true;
}

bool telegram_chat_ids_contains(const int64_t *ids, size_t count, int64_t chat_id)
{
    if (!ids || count == 0 || chat_id == 0) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (ids[i] == chat_id) {
            return true;
        }
    }
    return false;
}

int64_t telegram_chat_ids_resolve_target(const int64_t *ids,
                                         size_t count,
                                         int64_t primary_chat_id,
                                         int64_t requested_chat_id)
{
    if (requested_chat_id == 0) {
        return primary_chat_id;
    }

    if (telegram_chat_ids_contains(ids, count, requested_chat_id)) {
        return requested_chat_id;
    }

    return 0;
}
