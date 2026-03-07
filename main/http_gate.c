#include "http_gate.h"

#ifndef TEST_BUILD

#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "http_gate";

static StaticSemaphore_t s_http_gate_buf;
static SemaphoreHandle_t s_http_gate = NULL;

esp_err_t http_gate_init(void)
{
    if (s_http_gate) {
        return ESP_OK;
    }

    s_http_gate = xSemaphoreCreateMutexStatic(&s_http_gate_buf);
    if (!s_http_gate) {
        ESP_LOGE(TAG, "Failed to create HTTP gate mutex");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool http_gate_acquire(const char *owner, TickType_t timeout_ticks)
{
    if (http_gate_init() != ESP_OK) {
        return false;
    }

    if (xSemaphoreTake(s_http_gate, 0) == pdTRUE) {
        return true;
    }

    if (timeout_ticks == 0) {
        return false;
    }

    ESP_LOGI(TAG, "Waiting for HTTP gate (%s)", owner ? owner : "unknown");
    if (xSemaphoreTake(s_http_gate, timeout_ticks) == pdTRUE) {
        return true;
    }

    ESP_LOGW(TAG, "Timed out waiting for HTTP gate (%s)", owner ? owner : "unknown");
    return false;
}

void http_gate_release(void)
{
    if (s_http_gate) {
        xSemaphoreGive(s_http_gate);
    }
}

#endif  // !TEST_BUILD
