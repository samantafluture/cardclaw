#include "channel_local.h"
#include "cardclaw_hal.h"
#include "../messages.h"
#include "../config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "ch_local";

static QueueHandle_t s_input_queue;   // shared inbound → agent
static QueueHandle_t s_output_queue;  // display output from agent
static line_buffer_t s_line;          // keyboard line buffer

// ---------------------------------------------------------------------------
// Line buffer helpers
// ---------------------------------------------------------------------------

static void line_clear(void)
{
    memset(s_line.buf, 0, MAX_INPUT_LEN);
    s_line.cursor = 0;
    s_line.len = 0;
}

static void line_insert(char ch)
{
    if (s_line.len >= MAX_INPUT_LEN - 1) return;
    // Insert at cursor, shift right
    memmove(s_line.buf + s_line.cursor + 1,
            s_line.buf + s_line.cursor,
            s_line.len - s_line.cursor);
    s_line.buf[s_line.cursor] = ch;
    s_line.cursor++;
    s_line.len++;
    s_line.buf[s_line.len] = '\0';
}

static void line_backspace(void)
{
    if (s_line.cursor == 0) return;
    memmove(s_line.buf + s_line.cursor - 1,
            s_line.buf + s_line.cursor,
            s_line.len - s_line.cursor);
    s_line.cursor--;
    s_line.len--;
    s_line.buf[s_line.len] = '\0';
}

// ---------------------------------------------------------------------------
// Keyboard input task: reads key events → builds line → submits to agent
// ---------------------------------------------------------------------------

static void local_input_task(void *arg)
{
    (void)arg;
    line_clear();

    // Initial display state
    hal_display_status_bar("CardClaw", "WiFi");
    hal_display_render_input("", 0);

    while (1) {
        key_event_t evt = hal_keyboard_read_event();

        switch (evt.type) {
        case KEY_EVENT_CHAR:
            line_insert(evt.ch);
            break;

        case KEY_EVENT_BACKSPACE:
            line_backspace();
            break;

        case KEY_EVENT_CTRL_U:
            line_clear();
            break;

        case KEY_EVENT_ENTER:
            if (s_line.len > 0) {
                // Show user message on display
                char user_line[SCREEN_COLS + 4];
                snprintf(user_line, sizeof(user_line), "> %s", s_line.buf);
                hal_display_append_conv(user_line, COLOR_USER_MSG);

                // Submit to agent via shared inbound queue
                channel_msg_t msg = { .source = MSG_SOURCE_CHANNEL, .chat_id = 0 };
                strncpy(msg.text, s_line.buf, CHANNEL_RX_BUF_SIZE - 1);
                msg.text[CHANNEL_RX_BUF_SIZE - 1] = '\0';

                if (xQueueSend(s_input_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                    hal_display_append_conv("[queue full]", COLOR_ERROR);
                }

                line_clear();
            }
            break;

        case KEY_EVENT_CTRL_L:
            hal_display_clear();
            hal_display_status_bar("CardClaw", "WiFi");
            break;

        case KEY_EVENT_CTRL_C:
            // TODO: signal agent to cancel current generation
            hal_display_append_conv("[cancelled]", COLOR_ERROR);
            break;

        case KEY_EVENT_FN_UP:
            hal_display_scroll_up(3);
            continue;  // skip input render

        case KEY_EVENT_FN_DOWN:
            // Scroll down (towards newest)
            // hal_display_scroll_down would be the inverse — for now, noop
            continue;

        default:
            continue;
        }

        // Update input bar after every keystroke
        hal_display_render_input(s_line.buf, s_line.cursor);
    }
}

// ---------------------------------------------------------------------------
// Display output task: reads agent responses → renders on display
// ---------------------------------------------------------------------------

static void local_output_task(void *arg)
{
    (void)arg;
    channel_output_msg_t msg;

    while (1) {
        if (xQueueReceive(s_output_queue, &msg, portMAX_DELAY) == pdTRUE) {
            hal_display_append_conv(msg.text, COLOR_AGENT_MSG);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t channel_local_start(QueueHandle_t input_queue,
                               QueueHandle_t output_queue)
{
    s_input_queue = input_queue;
    s_output_queue = output_queue;

    line_clear();

    if (xTaskCreate(local_input_task, "local_in", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create local input task");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(local_output_task, "local_out", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create local output task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Local channel started (keyboard → agent → display)");
    return ESP_OK;
}
