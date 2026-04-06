#ifndef TELEGRAM_CHAT_IDS_H
#define TELEGRAM_CHAT_IDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEGRAM_MAX_ALLOWED_CHAT_IDS 4

// Parse a comma-separated chat ID list (or single ID) into out_ids.
// Returns true when at least one valid ID was parsed.
bool telegram_chat_ids_parse(const char *input,
                             int64_t *out_ids,
                             size_t max_ids,
                             size_t *out_count);

// Returns true when chat_id is present in the provided list.
bool telegram_chat_ids_contains(const int64_t *ids,
                                size_t count,
                                int64_t chat_id);

// Resolve outbound target chat routing.
// - requested_chat_id == 0: use primary_chat_id
// - requested_chat_id allowed: use requested_chat_id
// - requested_chat_id unauthorized: return 0 (drop)
int64_t telegram_chat_ids_resolve_target(const int64_t *ids,
                                         size_t count,
                                         int64_t primary_chat_id,
                                         int64_t requested_chat_id);

#endif // TELEGRAM_CHAT_IDS_H
