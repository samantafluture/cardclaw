#ifndef HTTP_GATE_H
#define HTTP_GATE_H

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef TEST_BUILD
static inline esp_err_t http_gate_init(void)
{
    return ESP_OK;
}

static inline bool http_gate_acquire(const char *owner, TickType_t timeout_ticks)
{
    (void)owner;
    (void)timeout_ticks;
    return true;
}

static inline void http_gate_release(void)
{
}
#else
esp_err_t http_gate_init(void);
bool http_gate_acquire(const char *owner, TickType_t timeout_ticks);
void http_gate_release(void);
#endif

#endif  // HTTP_GATE_H
