#ifndef CARDCLAW_HAL_H
#define CARDCLAW_HAL_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Display (ST7789V2 SPI)
// ---------------------------------------------------------------------------

// Screen geometry at 8×12 font on 240×135
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      135
#define FONT_WIDTH          8
#define FONT_HEIGHT         12
#define SCREEN_COLS         (DISPLAY_WIDTH / FONT_WIDTH)    // 30
#define SCREEN_ROWS         (DISPLAY_HEIGHT / FONT_HEIGHT)  // 11
#define STATUS_ROW          0
#define INPUT_ROW           (SCREEN_ROWS - 1)               // 10
#define CONV_FIRST_ROW      1
#define CONV_LAST_ROW       (SCREEN_ROWS - 2)               // 9
#define CONV_ROWS           (CONV_LAST_ROW - CONV_FIRST_ROW + 1)  // 9

// Scroll buffer
#define SCROLL_BUF_LINES    100

// Colors (RGB565)
#define COLOR_BG            0x0000  // Black
#define COLOR_TEXT           0x07E0  // Green
#define COLOR_STATUS_BG     0x18E3  // Dark gray
#define COLOR_STATUS_FG     0xFFFF  // White
#define COLOR_INPUT_BG      0x0841  // Very dark gray
#define COLOR_CURSOR         0x07E0  // Green
#define COLOR_USER_MSG      0x07E0  // Green
#define COLOR_AGENT_MSG     0xFFFF  // White
#define COLOR_ERROR         0xF800  // Red
#define COLOR_TOOL_MSG      0xFD20  // Orange

esp_err_t hal_display_init(void);
void hal_display_clear(void);
void hal_display_set_cursor(uint8_t col, uint8_t row);
void hal_display_print(const char *text);
void hal_display_print_line(uint8_t row, const char *text);
void hal_display_scroll_up(uint8_t lines);
void hal_display_set_brightness(uint8_t percent);
void hal_display_status_bar(const char *left, const char *right);
void hal_display_render_input(const char *text, uint16_t cursor_pos);
void hal_display_append_conv(const char *text, uint16_t color);

// ---------------------------------------------------------------------------
// Keyboard (TCA8418 I2C)
// ---------------------------------------------------------------------------

#define MAX_INPUT_LEN       256

typedef struct {
    char buf[MAX_INPUT_LEN];
    uint16_t cursor;
    uint16_t len;
} line_buffer_t;

// Modifier bitmask
#define MOD_SHIFT           0x01
#define MOD_FN              0x02
#define MOD_CTRL            0x04
#define MOD_ALT             0x08
#define MOD_OPT             0x10

// Key events pushed to the local channel
typedef enum {
    KEY_EVENT_CHAR,         // Regular character
    KEY_EVENT_ENTER,        // Submit message
    KEY_EVENT_BACKSPACE,
    KEY_EVENT_CTRL_U,       // Clear line
    KEY_EVENT_CTRL_L,       // Clear screen
    KEY_EVENT_CTRL_C,       // Cancel generation
    KEY_EVENT_FN_UP,        // Scroll up
    KEY_EVENT_FN_DOWN,      // Scroll down
    KEY_EVENT_FN_B,         // Brightness
    KEY_EVENT_FN_W,         // WiFi settings
    KEY_EVENT_FN_M,         // Menu
    KEY_EVENT_FN_S,         // Save conversation
} key_event_type_t;

typedef struct {
    key_event_type_t type;
    char ch;                // Valid when type == KEY_EVENT_CHAR
} key_event_t;

esp_err_t hal_keyboard_init(void);
bool hal_keyboard_event_available(void);
key_event_t hal_keyboard_read_event(void);  // blocks until event

// ---------------------------------------------------------------------------
// IMU (BMI270 I2C) — Phase 2
// ---------------------------------------------------------------------------

esp_err_t hal_imu_init(void);
void hal_imu_read(float *ax, float *ay, float *az,
                  float *gx, float *gy, float *gz);
bool hal_imu_shake_detected(void);

// ---------------------------------------------------------------------------
// IR — Phase 2
// ---------------------------------------------------------------------------

esp_err_t hal_ir_init(void);
esp_err_t hal_ir_send(const uint8_t *data, size_t len, uint16_t protocol);
esp_err_t hal_ir_receive(uint8_t *buf, size_t *len, uint32_t timeout_ms);

// ---------------------------------------------------------------------------
// Audio (ES8311 I2S) — Phase 2
// ---------------------------------------------------------------------------

esp_err_t hal_audio_init(void);
void hal_audio_beep(uint16_t freq_hz, uint16_t duration_ms);

// ---------------------------------------------------------------------------
// Storage (microSD) — Phase 2
// ---------------------------------------------------------------------------

esp_err_t hal_sd_init(void);
bool hal_sd_mounted(void);

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------

uint8_t hal_battery_percent(void);

#endif // CARDCLAW_HAL_H
