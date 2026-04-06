#include "local_admin.h"

#include "agent_commands.h"
#include "boot_guard.h"
#include "channel.h"
#include "config.h"
#include "memory.h"
#include "nvs_keys.h"
#include "wifi_credentials.h"

#include <stdio.h>
#include <string.h>

#ifndef TEST_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

static const char *TAG = "local_admin";
#else
#include "mock_esp.h"
#endif

static bool s_safe_mode = false;
static bool s_device_configured = false;

#ifndef TEST_BUILD
static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_wifi_stack_ready = false;
static bool s_wifi_netif_ready = false;
static bool s_wifi_handlers_registered = false;
static bool s_wifi_started = false;
static bool s_wifi_connected = false;
static bool s_wifi_connect_in_progress = false;
static int s_wifi_retry_num = 0;
static uint8_t s_last_disconnect_reason = 0;
static char s_target_ssid[64] = {0};
static char s_current_ip[16] = {0};

#define LOCAL_ADMIN_WIFI_CONNECTED_BIT BIT0
#define LOCAL_ADMIN_WIFI_FAIL_BIT      BIT1
#define LOCAL_ADMIN_WIFI_SCAN_LIMIT    6

#ifndef WIFI_REASON_BEACON_TIMEOUT
#define WIFI_REASON_BEACON_TIMEOUT 200
#endif
#ifndef WIFI_REASON_NO_AP_FOUND
#define WIFI_REASON_NO_AP_FOUND 201
#endif
#ifndef WIFI_REASON_AUTH_FAIL
#define WIFI_REASON_AUTH_FAIL 202
#endif
#ifndef WIFI_REASON_ASSOC_FAIL
#define WIFI_REASON_ASSOC_FAIL 203
#endif
#ifndef WIFI_REASON_HANDSHAKE_TIMEOUT
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#endif

static const char *wifi_disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
        case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
        case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
        case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
        case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
        case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
        default: return "UNKNOWN";
    }
}

static int wifi_channel_to_mhz(uint8_t channel)
{
    if (channel == 14) {
        return 2484;
    }
    if (channel >= 1 && channel <= 13) {
        return 2407 + (5 * channel);
    }
    if (channel >= 32 && channel <= 196) {
        return 5000 + (5 * channel);
    }
    return 0;
}

static const char *wifi_authmode_name(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
        default: return "UNKNOWN";
    }
}

static void local_admin_wifi_event_handler(void *arg,
                                           esp_event_base_t event_base,
                                           int32_t event_id,
                                           void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_started = true;
        ESP_LOGI(TAG, "WiFi STA started");
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        s_wifi_connected = false;
        s_current_ip[0] = '\0';
        s_last_disconnect_reason = event ? event->reason : 0;
        ESP_LOGW(TAG, "WiFi disconnected: reason=%u (%s)",
                 s_last_disconnect_reason,
                 wifi_disconnect_reason_name(s_last_disconnect_reason));

        if (s_wifi_connect_in_progress && s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "WiFi retry %d/%d", s_wifi_retry_num, WIFI_MAX_RETRY);
            return;
        }

        if (s_wifi_connect_in_progress && s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, LOCAL_ADMIN_WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;
        s_wifi_connect_in_progress = false;
        s_wifi_retry_num = 0;
        snprintf(s_current_ip, sizeof(s_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected: %s", s_current_ip);
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, LOCAL_ADMIN_WIFI_CONNECTED_BIT);
        }
    }
}

static bool load_wifi_credentials(char *ssid,
                                  size_t ssid_len,
                                  char *pass,
                                  size_t pass_len,
                                  char *error,
                                  size_t error_len)
{
    if (!memory_get(NVS_KEY_WIFI_SSID, ssid, ssid_len) || ssid[0] == '\0') {
#if defined(CONFIG_ZCLAW_WIFI_SSID)
        if (CONFIG_ZCLAW_WIFI_SSID[0] == '\0') {
            if (error && error_len > 0) {
                snprintf(error, error_len, "WiFi is not provisioned");
            }
            return false;
        }
        strncpy(ssid, CONFIG_ZCLAW_WIFI_SSID, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
#else
        if (error && error_len > 0) {
            snprintf(error, error_len, "WiFi is not provisioned");
        }
        return false;
#endif
    }

    if (!memory_get(NVS_KEY_WIFI_PASS, pass, pass_len)) {
#if defined(CONFIG_ZCLAW_WIFI_PASSWORD)
        strncpy(pass, CONFIG_ZCLAW_WIFI_PASSWORD, pass_len - 1);
        pass[pass_len - 1] = '\0';
#else
        pass[0] = '\0';
#endif
    }

    if (!wifi_credentials_validate(ssid, pass, error, error_len)) {
        return false;
    }

    return true;
}

static esp_err_t ensure_wifi_stack_ready(void)
{
    esp_err_t err;

    if (s_wifi_stack_ready) {
        return ESP_OK;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (!s_wifi_netif_ready) {
        esp_netif_create_default_wifi_sta();
        s_wifi_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (!s_wifi_handlers_registered) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         &local_admin_wifi_event_handler, NULL);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         &local_admin_wifi_event_handler, NULL);
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_handlers_registered = true;
    }

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
        if (!s_wifi_event_group) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_wifi_stack_ready = true;
    return ESP_OK;
}

static esp_err_t ensure_wifi_started(void)
{
    esp_err_t err = ensure_wifi_stack_ready();
    if (err != ESP_OK) {
        return err;
    }

    if (s_wifi_started) {
        return ESP_OK;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        return err;
    }

    s_wifi_started = true;
    return ESP_OK;
}

static bool format_wifi_status(char *result, size_t result_len)
{
    char stored_ssid[64] = {0};
    char pass[64] = {0};
    char wifi_error[96] = {0};
    wifi_ap_record_t ap_info = {0};
    bool have_credentials = load_wifi_credentials(stored_ssid, sizeof(stored_ssid),
                                                  pass, sizeof(pass),
                                                  wifi_error, sizeof(wifi_error));
    const char *ssid_text = s_target_ssid[0] != '\0'
        ? s_target_ssid
        : (have_credentials ? stored_ssid : "(none)");
    esp_err_t ap_info_err = ESP_FAIL;

    if (s_wifi_connected) {
        ap_info_err = esp_wifi_sta_get_ap_info(&ap_info);
    }

    if (s_wifi_connected && ap_info_err == ESP_OK) {
        snprintf(result, result_len,
                 "WiFi status: provisioned=%s safe_mode=%s driver=%s link=%s ssid=%s ip=%s rssi=%d last_reason=%s",
                 have_credentials ? "yes" : "no",
                 s_safe_mode ? "yes" : "no",
                 s_wifi_started ? "started" : "down",
                 s_wifi_connected ? "connected" : "idle",
                 ssid_text,
                 s_current_ip[0] != '\0' ? s_current_ip : "(none)",
                 ap_info.rssi,
                 s_last_disconnect_reason ? wifi_disconnect_reason_name(s_last_disconnect_reason) : "none");
        return true;
    }

    snprintf(result, result_len,
             "WiFi status: provisioned=%s safe_mode=%s driver=%s link=%s ssid=%s ip=%s rssi=%s last_reason=%s",
             have_credentials ? "yes" : "no",
             s_safe_mode ? "yes" : "no",
             s_wifi_started ? "started" : "down",
             s_wifi_connected ? "connected" : "idle",
             ssid_text,
             s_current_ip[0] != '\0' ? s_current_ip : "(none)",
             "(n/a)",
             s_last_disconnect_reason ? wifi_disconnect_reason_name(s_last_disconnect_reason) : "none");

    return true;
}

static bool format_wifi_scan(char *result, size_t result_len)
{
    esp_err_t err;
    wifi_scan_config_t scan_cfg = {0};
    wifi_ap_record_t records[LOCAL_ADMIN_WIFI_SCAN_LIMIT];
    uint16_t ap_total = 0;
    uint16_t record_count = LOCAL_ADMIN_WIFI_SCAN_LIMIT;
    char *cursor = result;
    size_t remaining = result_len;
    int written;
    int i;

    err = ensure_wifi_started();
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to start WiFi scan subsystem (%s)", esp_err_to_name(err));
        return false;
    }

    scan_cfg.show_hidden = false;
    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: WiFi scan failed to start (%s)", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_scan_get_ap_num(&ap_total);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: WiFi scan count unavailable (%s)", esp_err_to_name(err));
        return false;
    }

    if (ap_total == 0) {
        snprintf(result, result_len, "WiFi scan: 0 APs visible");
        return true;
    }

    if (record_count > ap_total) {
        record_count = ap_total;
    }
    err = esp_wifi_scan_get_ap_records(&record_count, records);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: WiFi scan results unavailable (%s)", esp_err_to_name(err));
        return false;
    }

    written = snprintf(cursor, remaining, "WiFi scan: %u APs visible (top %u)", ap_total, record_count);
    if (written < 0 || (size_t)written >= remaining) {
        snprintf(result, result_len, "Error: WiFi scan output overflow");
        return false;
    }
    cursor += (size_t)written;
    remaining -= (size_t)written;

    for (i = 0; i < record_count; i++) {
        int mhz = wifi_channel_to_mhz(records[i].primary);
        written = snprintf(cursor, remaining,
                           "\n- %s rssi=%d auth=%s ch=%u%s",
                           records[i].ssid[0] ? (const char *)records[i].ssid : "<hidden>",
                           records[i].rssi,
                           wifi_authmode_name(records[i].authmode),
                           records[i].primary,
                           mhz > 0 ? "" : "");
        if (written < 0 || (size_t)written >= remaining) {
            snprintf(result, result_len, "Error: WiFi scan output overflow");
            return false;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;

        if (mhz > 0) {
            written = snprintf(cursor, remaining, " (%dMHz)", mhz);
            if (written < 0 || (size_t)written >= remaining) {
                snprintf(result, result_len, "Error: WiFi scan output overflow");
                return false;
            }
            cursor += (size_t)written;
            remaining -= (size_t)written;
        }
    }

    return true;
}
#else
static char s_test_wifi_status[256] = "WiFi status: mock";
static char s_test_wifi_scan[256] = "WiFi scan: mock";
static local_admin_action_t s_test_last_action = LOCAL_ADMIN_ACTION_NONE;

static bool format_wifi_status(char *result, size_t result_len)
{
    snprintf(result, result_len, "%s", s_test_wifi_status);
    return true;
}

static bool format_wifi_scan(char *result, size_t result_len)
{
    snprintf(result, result_len, "%s", s_test_wifi_scan);
    return true;
}
#endif

void local_admin_set_safe_mode(bool safe_mode)
{
    s_safe_mode = safe_mode;
}

void local_admin_set_device_configured(bool device_configured)
{
    s_device_configured = device_configured;
}

bool local_admin_is_command(const char *message)
{
    return agent_is_command(message, "reboot") ||
           agent_is_command(message, "wifi") ||
           agent_is_command(message, "bootcount") ||
           agent_is_command(message, "factory-reset");
}

bool local_admin_handle_command(const char *message,
                                char *result,
                                size_t result_len,
                                local_admin_action_t *action_out)
{
    const char *payload = NULL;
    char payload_buf[64];
    char *token = NULL;
    char *extra = NULL;

    if (!message || !result || result_len == 0) {
        return false;
    }

    if (action_out) {
        *action_out = LOCAL_ADMIN_ACTION_NONE;
    }

    if (agent_is_command(message, "reboot")) {
        payload = agent_command_payload(message, "reboot");
        if (payload && payload[0] != '\0') {
            snprintf(result, result_len, "Error: /reboot does not take arguments");
            return false;
        }
        if (action_out) {
            *action_out = LOCAL_ADMIN_ACTION_REBOOT;
        }
        snprintf(result, result_len, "Rebooting...");
        return true;
    }

    if (agent_is_command(message, "bootcount")) {
        int boot_count = boot_guard_get_persisted_count();
        int remaining_before_safe = MAX_BOOT_FAILURES - boot_count;
        if (remaining_before_safe < 0) {
            remaining_before_safe = 0;
        }

        payload = agent_command_payload(message, "bootcount");
        if (payload && payload[0] != '\0') {
            snprintf(result, result_len, "Error: /bootcount does not take arguments");
            return false;
        }

        snprintf(result, result_len,
                 "Boot count: persisted=%d max_failures=%d remaining_before_safe=%d safe_mode=%s configured=%s",
                 boot_count,
                 MAX_BOOT_FAILURES,
                 remaining_before_safe,
                 s_safe_mode ? "yes" : "no",
                 s_device_configured ? "yes" : "no");
        return true;
    }

    if (agent_is_command(message, "factory-reset")) {
        payload = agent_command_payload(message, "factory-reset");
        if (!payload || payload[0] == '\0') {
            snprintf(result, result_len,
                     "Factory reset will erase WiFi credentials, tokens, schedules, memories, and boot state. Run /factory-reset confirm to continue.");
            return true;
        }

        if (strlen(payload) >= sizeof(payload_buf)) {
            snprintf(result, result_len, "Error: /factory-reset arguments too long");
            return false;
        }

        snprintf(payload_buf, sizeof(payload_buf), "%s", payload);
        token = strtok(payload_buf, " \t\r\n");
        extra = strtok(NULL, " \t\r\n");
        if (!token || strcmp(token, "confirm") != 0 || extra != NULL) {
            snprintf(result, result_len, "Error: use /factory-reset confirm");
            return false;
        }

        if (action_out) {
            *action_out = LOCAL_ADMIN_ACTION_FACTORY_RESET_REBOOT;
        }
        snprintf(result, result_len, "Factory reset confirmed. Erasing NVS and rebooting...");
        return true;
    }

    if (agent_is_command(message, "wifi")) {
        payload = agent_command_payload(message, "wifi");
        if (!payload || payload[0] == '\0' || strcmp(payload, "status") == 0) {
            return format_wifi_status(result, result_len);
        }

        if (strlen(payload) >= sizeof(payload_buf)) {
            snprintf(result, result_len, "Error: /wifi arguments too long");
            return false;
        }

        snprintf(payload_buf, sizeof(payload_buf), "%s", payload);
        token = strtok(payload_buf, " \t\r\n");
        extra = strtok(NULL, " \t\r\n");
        if (!token) {
            return format_wifi_status(result, result_len);
        }
        if (extra != NULL) {
            snprintf(result, result_len, "Error: /wifi takes at most one argument");
            return false;
        }
        if (strcmp(token, "status") == 0) {
            return format_wifi_status(result, result_len);
        }
        if (strcmp(token, "scan") == 0) {
            return format_wifi_scan(result, result_len);
        }

        snprintf(result, result_len, "Error: unknown /wifi argument '%s' (use status or scan)", token);
        return false;
    }

    snprintf(result, result_len, "Error: unknown local admin command");
    return false;
}

void local_admin_perform_action(local_admin_action_t action)
{
#ifdef TEST_BUILD
    s_test_last_action = action;
    if (action == LOCAL_ADMIN_ACTION_FACTORY_RESET_REBOOT) {
        (void)memory_factory_reset();
    }
#else
    if (action == LOCAL_ADMIN_ACTION_NONE) {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(250));

    if (action == LOCAL_ADMIN_ACTION_FACTORY_RESET_REBOOT) {
        esp_err_t err = memory_factory_reset();
        if (err != ESP_OK) {
            channel_write("\r\nFactory reset failed.\r\n\r\n");
            return;
        }
    }

    esp_restart();
#endif
}

bool local_admin_wifi_connect_from_store(void)
{
#ifdef TEST_BUILD
    return s_device_configured;
#else
    char ssid[64] = {0};
    char pass[64] = {0};
    char wifi_error[96] = {0};
    esp_err_t err;
    wifi_config_t wifi_config = {0};
    EventBits_t bits;

    if (!load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass), wifi_error, sizeof(wifi_error))) {
        ESP_LOGE(TAG, "%s", wifi_error);
        return false;
    }

    err = ensure_wifi_started();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        return false;
    }

    strncpy(s_target_ssid, ssid, sizeof(s_target_ssid) - 1);
    s_target_ssid[sizeof(s_target_ssid) - 1] = '\0';
    s_current_ip[0] = '\0';
    s_last_disconnect_reason = 0;
    s_wifi_retry_num = 0;
    s_wifi_connected = false;
    s_wifi_connect_in_progress = true;
    xEventGroupClearBits(s_wifi_event_group,
                         LOCAL_ADMIN_WIFI_CONNECTED_BIT | LOCAL_ADMIN_WIFI_FAIL_BIT);

    wifi_credentials_copy_to_sta_config(wifi_config.sta.ssid, wifi_config.sta.password, ssid, pass);
    wifi_config.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure WiFi: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connect: %s", esp_err_to_name(err));
        return false;
    }

    bits = xEventGroupWaitBits(s_wifi_event_group,
                               LOCAL_ADMIN_WIFI_CONNECTED_BIT | LOCAL_ADMIN_WIFI_FAIL_BIT,
                               pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & LOCAL_ADMIN_WIFI_CONNECTED_BIT) != 0;
#endif
}

#ifdef TEST_BUILD
void local_admin_test_reset(void)
{
    s_safe_mode = false;
    s_device_configured = false;
    snprintf(s_test_wifi_status, sizeof(s_test_wifi_status), "%s", "WiFi status: mock");
    snprintf(s_test_wifi_scan, sizeof(s_test_wifi_scan), "%s", "WiFi scan: mock");
    s_test_last_action = LOCAL_ADMIN_ACTION_NONE;
}

void local_admin_test_set_wifi_status(const char *status_text)
{
    snprintf(s_test_wifi_status, sizeof(s_test_wifi_status), "%s",
             status_text ? status_text : "WiFi status: mock");
}

void local_admin_test_set_wifi_scan(const char *scan_text)
{
    snprintf(s_test_wifi_scan, sizeof(s_test_wifi_scan), "%s",
             scan_text ? scan_text : "WiFi scan: mock");
}

local_admin_action_t local_admin_test_last_action(void)
{
    return s_test_last_action;
}
#endif
