#ifndef TELEGRAM_POLL_POLICY_H
#define TELEGRAM_POLL_POLICY_H

#include <stdbool.h>

#include "config.h"

// Return Telegram long-poll timeout (seconds) for a given LLM backend.
int telegram_poll_timeout_for_backend(llm_backend_t backend);

#ifdef TEST_BUILD
int telegram_poll_timeout_for_backend_test(llm_backend_t backend, bool classic_esp32_target);
#endif

#endif // TELEGRAM_POLL_POLICY_H
