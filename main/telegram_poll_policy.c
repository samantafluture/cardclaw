#include "telegram_poll_policy.h"

static int telegram_poll_timeout_for_backend_impl(llm_backend_t backend, bool classic_esp32_target)
{
    int timeout = TELEGRAM_POLL_TIMEOUT;

    if (backend == LLM_BACKEND_OPENROUTER && TELEGRAM_POLL_TIMEOUT_OPENROUTER < timeout) {
        timeout = TELEGRAM_POLL_TIMEOUT_OPENROUTER;
    }

    if (classic_esp32_target && TELEGRAM_POLL_TIMEOUT_ESP32 < timeout) {
        timeout = TELEGRAM_POLL_TIMEOUT_ESP32;
    }

    return timeout;
}

int telegram_poll_timeout_for_backend(llm_backend_t backend)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    return telegram_poll_timeout_for_backend_impl(backend, true);
#else
    return telegram_poll_timeout_for_backend_impl(backend, false);
#endif
}

#ifdef TEST_BUILD
int telegram_poll_timeout_for_backend_test(llm_backend_t backend, bool classic_esp32_target)
{
    return telegram_poll_timeout_for_backend_impl(backend, classic_esp32_target);
}
#endif
