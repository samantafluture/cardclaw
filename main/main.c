#include "config.h"
#include "memory.h"
#include "channel.h"
#include "agent.h"
#include "llm.h"
#include "tools.h"
#include "telegram.h"
#include "cron.h"
#include "ratelimit.h"
#include "ota.h"
#include "http_gate.h"
#include "boot_guard.h"
#include "local_admin.h"
#include "nvs_keys.h"
#include "messages.h"
#include "gpio_policy.h"
#include "hal/cardclaw_hal.h"
#include "hal/channel_local.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "driver/gpio.h"

static const char *TAG = "main";
static bool s_safe_mode = false;

static void fail_fast_startup(const char *component, esp_err_t err)
{
    ESP_LOGE(TAG, "Startup failure in %s: %s", component, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void clear_boot_count(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BOOT_SUCCESS_DELAY_MS));
    UBaseType_t start_hwm_words = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "boot_ok stack high-water mark at start: %u words",
             (unsigned)start_hwm_words);

    bool pending_before = ota_is_pending_verify();
    if (pending_before) {
        esp_err_t ota_err = ota_mark_valid_if_pending();
        if (ota_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to confirm OTA image: %s", esp_err_to_name(ota_err));
        } else {
            ESP_LOGI(TAG, "OTA image confirmed after stable boot window");
        }
    }

    esp_err_t boot_count_err = boot_guard_set_persisted_count(0);
    if (boot_count_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear boot counter: %s", esp_err_to_name(boot_count_err));
    } else {
        ESP_LOGI(TAG, "Boot counter cleared - system stable");
    }
    UBaseType_t end_hwm_words = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "boot_ok stack high-water mark before exit: %u words",
             (unsigned)end_hwm_words);
    vTaskDelete(NULL);
}

// Check factory reset button
static bool check_factory_reset(void)
{
    if (!gpio_policy_runtime_input_pin_is_safe(FACTORY_RESET_PIN)) {
        ESP_LOGW(TAG, "Skipping factory reset button check: pin %d is unsafe on this target", FACTORY_RESET_PIN);
        return false;
    }

    gpio_reset_pin(FACTORY_RESET_PIN);
    gpio_set_direction(FACTORY_RESET_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(FACTORY_RESET_PIN, GPIO_PULLUP_ONLY);

    // Check if button is held low
    if (gpio_get_level(FACTORY_RESET_PIN) == 0) {
        ESP_LOGW(TAG, "Factory reset button detected, hold for 5 seconds...");

        int held_ms = 0;
        while (gpio_get_level(FACTORY_RESET_PIN) == 0 && held_ms < FACTORY_RESET_HOLD_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
            held_ms += 100;
        }

        if (held_ms >= FACTORY_RESET_HOLD_MS) {
            ESP_LOGW(TAG, "Factory reset triggered!");
            ESP_ERROR_CHECK(memory_factory_reset());
            ESP_LOGI(TAG, "Factory reset complete, restarting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return true;
        }
    }
    return false;
}

static bool device_is_configured(void)
{
    char ssid[64] = {0};
    if (memory_get(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid)) && ssid[0] != '\0') {
        return true;
    }

#if defined(CONFIG_ZCLAW_WIFI_SSID)
    return CONFIG_ZCLAW_WIFI_SSID[0] != '\0';
#else
    return false;
#endif
}

static void print_provisioning_help(void)
{
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "  Device is not provisioned");
    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "Run on host:");
    ESP_LOGE(TAG, "  ./scripts/provision.sh --port <serial-port>");
    ESP_LOGE(TAG, "Then restart the board.");
    ESP_LOGE(TAG, "");
}

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  zclaw v%s", ota_get_version());
    ESP_LOGI(TAG, "  AI Agent on ESP32");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 1. Initialize NVS
    ESP_ERROR_CHECK(memory_init());
    ESP_ERROR_CHECK(http_gate_init());

    // 2. Initialize OTA (check for pending rollback)
    ota_init();

    // 3. Check factory reset button
#if !CONFIG_ZCLAW_EMULATOR_MODE
    check_factory_reset();
#endif

    // 4. Boot loop protection
#if !CONFIG_ZCLAW_EMULATOR_MODE
    int boot_count = boot_guard_get_persisted_count();
    int next_boot_count = boot_guard_next_count(boot_count);
    esp_err_t boot_count_err = boot_guard_set_persisted_count(next_boot_count);
    if (boot_count_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist boot counter: %s", esp_err_to_name(boot_count_err));
    }

    if (boot_guard_should_enter_safe_mode(boot_count, MAX_BOOT_FAILURES)) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  SAFE MODE - Too many boot failures");
        ESP_LOGE(TAG, "  Hold BOOT button for factory reset");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "");
        s_safe_mode = true;
    }
#endif

    local_admin_set_safe_mode(s_safe_mode);

#if CONFIG_ZCLAW_EMULATOR_MODE
    ESP_LOGW(TAG, "Emulator mode enabled: skipping WiFi/NTP/Telegram startup");
#ifndef CONFIG_ZCLAW_STUB_LLM
    ESP_LOGW(TAG, "Stub LLM is disabled; without network, LLM requests may fail");
#endif

    ESP_ERROR_CHECK(llm_init());
    ratelimit_init();
    tools_init();
    channel_init();

    QueueHandle_t input_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(channel_msg_t));
    QueueHandle_t channel_output_queue = xQueueCreate(OUTPUT_QUEUE_LENGTH, sizeof(channel_output_msg_t));
    if (!input_queue || !channel_output_queue) {
        ESP_LOGE(TAG, "Failed to create emulator queues");
        esp_restart();
    }

    esp_err_t startup_err = channel_start(input_queue, channel_output_queue);
    if (startup_err != ESP_OK) {
        fail_fast_startup("channel_start", startup_err);
    }

    startup_err = agent_start(input_queue, channel_output_queue, NULL, NULL);
    if (startup_err != ESP_OK) {
        fail_fast_startup("agent_start", startup_err);
    }

    channel_write("\r\nzclaw emulator ready. Type a message and press Enter.\r\n\r\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else

    bool device_configured = device_is_configured();
    local_admin_set_device_configured(device_configured);

    // 5. Initialize LLM client for local serial commands
    ESP_ERROR_CHECK(llm_init());

    // 6. Initialize rate limiter
    ratelimit_init();

    // 7. Initialize Telegram config state
#if CONFIG_ZCLAW_STUB_TELEGRAM
    ESP_LOGW(TAG, "Telegram stub mode enabled; skipping Telegram startup");
#else
    esp_err_t telegram_init_err = telegram_init();  // Missing token is non-fatal
    if (telegram_init_err != ESP_OK && telegram_init_err != ESP_ERR_NOT_FOUND) {
        fail_fast_startup("telegram_init", telegram_init_err);
    }
#endif

    // 8. Register tools and local channel early so /gpio and /diag work before WiFi.
    tools_init();
    channel_init();

    QueueHandle_t input_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(channel_msg_t));
    QueueHandle_t channel_output_queue = xQueueCreate(OUTPUT_QUEUE_LENGTH, sizeof(channel_output_msg_t));
    QueueHandle_t display_output_queue = xQueueCreate(OUTPUT_QUEUE_LENGTH, sizeof(channel_output_msg_t));
    QueueHandle_t telegram_output_queue = NULL;
#if CONFIG_ZCLAW_STUB_TELEGRAM
    bool telegram_enabled = false;
#else
    bool telegram_enabled = device_configured && !s_safe_mode && telegram_is_configured();
#endif
    if (telegram_enabled) {
        telegram_output_queue = xQueueCreate(TELEGRAM_OUTPUT_QUEUE_LENGTH, sizeof(telegram_msg_t));
    }

    if (!input_queue || !channel_output_queue || !display_output_queue ||
        (telegram_enabled && !telegram_output_queue)) {
        ESP_LOGE(TAG, "Failed to create queues");
        esp_restart();
    }

    // Initialize CardClaw HAL (display + keyboard)
    esp_err_t hal_err = hal_display_init();
    if (hal_err != ESP_OK) {
        ESP_LOGW(TAG, "Display init failed: %s (continuing headless)", esp_err_to_name(hal_err));
    }
    hal_err = hal_keyboard_init();
    if (hal_err != ESP_OK) {
        ESP_LOGW(TAG, "Keyboard init failed: %s (continuing headless)", esp_err_to_name(hal_err));
    }

    esp_err_t startup_err = channel_start(input_queue, channel_output_queue);
    if (startup_err != ESP_OK) {
        fail_fast_startup("channel_start", startup_err);
    }

    // Start local channel (keyboard → agent → display)
    startup_err = channel_local_start(input_queue, display_output_queue);
    if (startup_err != ESP_OK) {
        ESP_LOGW(TAG, "Local channel start failed: %s", esp_err_to_name(startup_err));
    }

    startup_err = agent_start(input_queue, channel_output_queue, telegram_output_queue, display_output_queue);
    if (startup_err != ESP_OK) {
        fail_fast_startup("agent_start", startup_err);
    }

    // 9. Check if configured or in safe mode
    if (!device_configured || s_safe_mode) {
        if (s_safe_mode) {
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "========================================");
            ESP_LOGE(TAG, "  SAFE MODE - Too many boot failures");
            ESP_LOGE(TAG, "  Hold BOOT button for factory reset");
            ESP_LOGE(TAG, "========================================");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "Recovery options:");
            ESP_LOGE(TAG, "  1) Hold BOOT for factory reset");
            ESP_LOGE(TAG, "  2) Reflash firmware and reprovision");
            ESP_LOGE(TAG, "");
            channel_write("\r\nSAFE MODE - local serial commands remain available.\r\n"
                          "Try /gpio, /diag, /reboot, /wifi, /bootcount, /factory-reset, /help, or /settings.\r\n\r\n");
        } else {
            print_provisioning_help();
            channel_write("\r\nDevice is not provisioned.\r\n"
                          "Local serial commands remain available: /gpio, /diag, /reboot, /wifi, /bootcount, /factory-reset, /help, /settings.\r\n\r\n");
        }
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    // 10. Connect to WiFi
    if (!local_admin_wifi_connect_from_store()) {
        ESP_LOGE(TAG, "WiFi failed, restarting...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    // 11. Start task to clear boot counter after stable period
    if (xTaskCreate(clear_boot_count, "boot_ok", BOOT_OK_TASK_STACK_SIZE, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create boot confirmation task");
    }

    // 12. Initialize cron (includes NTP sync)
    ESP_ERROR_CHECK(cron_init());

    // 13. Start Telegram channel
    if (telegram_enabled) {
        startup_err = telegram_start(input_queue, telegram_output_queue);
        if (startup_err != ESP_OK) {
            fail_fast_startup("telegram_start", startup_err);
        }
    }

    // 14. Start cron task
    startup_err = cron_start(input_queue);
    if (startup_err != ESP_OK) {
        fail_fast_startup("cron_start", startup_err);
    }

    // 15. Print ready message
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Ready! Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 16. Send startup notification on Telegram
    if (telegram_enabled && telegram_is_configured()) {
        telegram_send_startup();
    }

    // app_main returns - FreeRTOS scheduler continues running tasks
#endif
}
