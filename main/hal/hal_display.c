#include "cardclaw_hal.h"
#include "font_8x12.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "hal_display";

// ---------------------------------------------------------------------------
// Cardputer ADV pin assignments (ST7789V2)
// ---------------------------------------------------------------------------
#define PIN_CS      GPIO_NUM_7
#define PIN_DC      GPIO_NUM_34
#define PIN_RST     GPIO_NUM_33
#define PIN_SCLK    GPIO_NUM_36
#define PIN_MOSI    GPIO_NUM_35
#define PIN_BL      GPIO_NUM_38

#define SPI_FREQ_HZ (40 * 1000 * 1000)  // 40 MHz
#define SPI_HOST_ID SPI2_HOST

// Backlight PWM
#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BL_LEDC_FREQ     5000
#define BL_LEDC_RES      LEDC_TIMER_8_BIT

// ---------------------------------------------------------------------------
// ST7789V2 commands
// ---------------------------------------------------------------------------
#define ST7789_NOP       0x00
#define ST7789_SWRESET   0x01
#define ST7789_SLPOUT    0x11
#define ST7789_NORON     0x13
#define ST7789_INVON     0x21
#define ST7789_DISPON    0x29
#define ST7789_CASET     0x2A
#define ST7789_RASET     0x2B
#define ST7789_RAMWR     0x2C
#define ST7789_COLMOD    0x3A
#define ST7789_MADCTL    0x36

// MADCTL flags for 240x135 landscape (rotated)
#define MADCTL_VALUE     0x70  // MX + MV + RGB

// The 240x135 panel sits at an offset within the controller's 240x320 memory
#define COL_OFFSET       40
#define ROW_OFFSET       53

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static spi_device_handle_t s_spi;
static SemaphoreHandle_t s_display_mutex;

// Screen text buffers
static char s_conv_buf[SCROLL_BUF_LINES][SCREEN_COLS + 1];  // circular scroll buffer
static uint16_t s_conv_colors[SCROLL_BUF_LINES];            // per-line color
static int s_scroll_head;    // next write position in circular buffer
static int s_scroll_count;   // total lines written (capped at SCROLL_BUF_LINES)
static int s_view_offset;    // 0 = bottom (most recent), positive = scrolled up

// Line pixel buffer for DMA transfer (one row of text = FONT_HEIGHT pixel rows)
// 240 pixels × 2 bytes (RGB565) × FONT_HEIGHT rows = 5760 bytes
static uint16_t s_line_buf[DISPLAY_WIDTH * FONT_HEIGHT]
    __attribute__((aligned(4)));

// ---------------------------------------------------------------------------
// SPI helpers
// ---------------------------------------------------------------------------

static void spi_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(PIN_DC, 0);  // command mode
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(PIN_DC, 1);  // data mode
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_data_byte(uint8_t val)
{
    spi_data(&val, 1);
}

// ---------------------------------------------------------------------------
// Display low-level
// ---------------------------------------------------------------------------

static void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += COL_OFFSET;
    x1 += COL_OFFSET;
    y0 += ROW_OFFSET;
    y1 += ROW_OFFSET;

    uint8_t ca[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    spi_cmd(ST7789_CASET);
    spi_data(ca, 4);

    uint8_t ra[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    spi_cmd(ST7789_RASET);
    spi_data(ra, 4);

    spi_cmd(ST7789_RAMWR);
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color)
{
    set_addr_window(x, y, x + w - 1, y + h - 1);

    // Swap bytes for SPI (big-endian)
    uint16_t swapped = (color >> 8) | (color << 8);
    size_t pixels = (size_t)w * h;

    // Fill line buffer and send in chunks
    size_t chunk = sizeof(s_line_buf) / sizeof(s_line_buf[0]);
    for (size_t i = 0; i < chunk && i < pixels; i++) {
        s_line_buf[i] = swapped;
    }

    gpio_set_level(PIN_DC, 1);
    while (pixels > 0) {
        size_t send = pixels < chunk ? pixels : chunk;
        spi_transaction_t t = {
            .length = send * 16,
            .tx_buffer = s_line_buf,
        };
        spi_device_polling_transmit(s_spi, &t);
        pixels -= send;
    }
}

// Render a single character at pixel position (px, py) with fg/bg colors
static void render_char(uint16_t px, uint16_t py, char ch,
                         uint16_t fg, uint16_t bg)
{
    uint16_t fg_sw = (fg >> 8) | (fg << 8);
    uint16_t bg_sw = (bg >> 8) | (bg << 8);

    uint8_t c = (uint8_t)ch;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = font_8x12[c - 0x20];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            s_line_buf[row * FONT_WIDTH + col] =
                (bits & (0x80 >> col)) ? fg_sw : bg_sw;
        }
    }

    set_addr_window(px, py, px + FONT_WIDTH - 1, py + FONT_HEIGHT - 1);
    gpio_set_level(PIN_DC, 1);
    spi_transaction_t t = {
        .length = FONT_WIDTH * FONT_HEIGHT * 16,
        .tx_buffer = s_line_buf,
    };
    spi_device_polling_transmit(s_spi, &t);
}

// Render a full text row (row 0..SCREEN_ROWS-1) with fg/bg colors
static void render_text_row(uint8_t row, const char *text,
                             uint16_t fg, uint16_t bg)
{
    uint16_t fg_sw = (fg >> 8) | (fg << 8);
    uint16_t bg_sw = (bg >> 8) | (bg << 8);
    uint16_t py = row * FONT_HEIGHT;
    size_t len = strlen(text);

    // Build full row pixel buffer (DISPLAY_WIDTH × FONT_HEIGHT)
    for (int prow = 0; prow < FONT_HEIGHT; prow++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            char ch = (col < (int)len) ? text[col] : ' ';
            uint8_t c = (uint8_t)ch;
            if (c < 0x20 || c > 0x7E) c = ' ';
            const uint8_t *glyph = font_8x12[c - 0x20];
            uint8_t bits = glyph[prow];
            for (int pcol = 0; pcol < FONT_WIDTH; pcol++) {
                s_line_buf[prow * DISPLAY_WIDTH + col * FONT_WIDTH + pcol] =
                    (bits & (0x80 >> pcol)) ? fg_sw : bg_sw;
            }
        }
    }

    set_addr_window(0, py, DISPLAY_WIDTH - 1, py + FONT_HEIGHT - 1);
    gpio_set_level(PIN_DC, 1);
    spi_transaction_t t = {
        .length = DISPLAY_WIDTH * FONT_HEIGHT * 16,
        .tx_buffer = s_line_buf,
    };
    spi_device_polling_transmit(s_spi, &t);
}

// ---------------------------------------------------------------------------
// Scroll buffer helpers
// ---------------------------------------------------------------------------

static int scroll_index(int offset_from_newest)
{
    // offset 0 = most recent line, offset 1 = one before, etc.
    int idx = s_scroll_head - 1 - offset_from_newest;
    while (idx < 0) idx += SCROLL_BUF_LINES;
    return idx % SCROLL_BUF_LINES;
}

static void refresh_conversation(void)
{
    int available = s_scroll_count;
    int offset = s_view_offset;

    for (int screen_row = 0; screen_row < CONV_ROWS; screen_row++) {
        int line_offset = offset + (CONV_ROWS - 1 - screen_row);
        if (line_offset < available) {
            int idx = scroll_index(line_offset);
            render_text_row(CONV_FIRST_ROW + screen_row,
                           s_conv_buf[idx], s_conv_colors[idx], COLOR_BG);
        } else {
            render_text_row(CONV_FIRST_ROW + screen_row, "", COLOR_TEXT, COLOR_BG);
        }
    }
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

static void backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES,
        .freq_hz = BL_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_BL,
        .duty = 200,  // ~78% brightness default
        .hpoint = 0,
    };
    ledc_channel_config(&ch_conf);
}

static void st7789_init_sequence(void)
{
    // Hardware reset
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_cmd(ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    spi_cmd(ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Pixel format: 16-bit RGB565
    spi_cmd(ST7789_COLMOD);
    spi_data_byte(0x55);

    // Display orientation: landscape
    spi_cmd(ST7789_MADCTL);
    spi_data_byte(MADCTL_VALUE);

    // Inversion on (ST7789 panels typically need this)
    spi_cmd(ST7789_INVON);

    spi_cmd(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    spi_cmd(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t hal_display_init(void)
{
    s_display_mutex = xSemaphoreCreateMutex();
    if (!s_display_mutex) return ESP_ERR_NO_MEM;

    // Configure control GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(s_line_buf),
    };
    esp_err_t err = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    backlight_init();
    st7789_init_sequence();

    // Clear scroll state
    memset(s_conv_buf, 0, sizeof(s_conv_buf));
    memset(s_conv_colors, 0, sizeof(s_conv_colors));
    s_scroll_head = 0;
    s_scroll_count = 0;
    s_view_offset = 0;

    hal_display_clear();

    ESP_LOGI(TAG, "Display initialized (%dx%d, %dx%d chars)",
             DISPLAY_WIDTH, DISPLAY_HEIGHT, SCREEN_COLS, SCREEN_ROWS);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void hal_display_clear(void)
{
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BG);
    xSemaphoreGive(s_display_mutex);
}

void hal_display_set_cursor(uint8_t col, uint8_t row)
{
    // Render cursor block at position
    if (col >= SCREEN_COLS || row >= SCREEN_ROWS) return;
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    render_char(col * FONT_WIDTH, row * FONT_HEIGHT, '_', COLOR_CURSOR, COLOR_BG);
    xSemaphoreGive(s_display_mutex);
}

void hal_display_print(const char *text)
{
    hal_display_append_conv(text, COLOR_TEXT);
}

void hal_display_print_line(uint8_t row, const char *text)
{
    if (row >= SCREEN_ROWS) return;
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    render_text_row(row, text, COLOR_TEXT, COLOR_BG);
    xSemaphoreGive(s_display_mutex);
}

void hal_display_scroll_up(uint8_t lines)
{
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    int max_offset = s_scroll_count > CONV_ROWS
                        ? s_scroll_count - CONV_ROWS : 0;
    s_view_offset += lines;
    if (s_view_offset > max_offset) s_view_offset = max_offset;
    refresh_conversation();
    xSemaphoreGive(s_display_mutex);
}

void hal_display_set_brightness(uint8_t percent)
{
    uint32_t duty = (uint32_t)percent * 255 / 100;
    if (duty > 255) duty = 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

void hal_display_status_bar(const char *left, const char *right)
{
    char bar[SCREEN_COLS + 1];
    memset(bar, ' ', SCREEN_COLS);
    bar[SCREEN_COLS] = '\0';

    size_t left_len = left ? strlen(left) : 0;
    size_t right_len = right ? strlen(right) : 0;

    if (left_len > SCREEN_COLS) left_len = SCREEN_COLS;
    if (left) memcpy(bar, left, left_len);

    if (right && right_len <= SCREEN_COLS) {
        size_t rstart = SCREEN_COLS - right_len;
        if (rstart > left_len + 1) {
            memcpy(bar + rstart, right, right_len);
        }
    }

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    render_text_row(STATUS_ROW, bar, COLOR_STATUS_FG, COLOR_STATUS_BG);
    xSemaphoreGive(s_display_mutex);
}

void hal_display_render_input(const char *text, uint16_t cursor_pos)
{
    char row[SCREEN_COLS + 1];
    memset(row, ' ', SCREEN_COLS);
    row[SCREEN_COLS] = '\0';

    // Prefix with "> "
    row[0] = '>';
    row[1] = ' ';

    size_t avail = SCREEN_COLS - 2;
    size_t len = text ? strlen(text) : 0;

    // If text is longer than available space, show the end (scroll with cursor)
    size_t start = 0;
    if (len > avail) {
        start = len - avail;
    }
    size_t copy_len = len - start;
    if (copy_len > avail) copy_len = avail;
    if (text) memcpy(row + 2, text + start, copy_len);

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    render_text_row(INPUT_ROW, row, COLOR_TEXT, COLOR_INPUT_BG);

    // Render cursor
    uint16_t cursor_col = 2 + (cursor_pos > start ? cursor_pos - start : 0);
    if (cursor_col < SCREEN_COLS) {
        char under = (cursor_col < (uint16_t)strlen(row)) ? row[cursor_col] : ' ';
        render_char(cursor_col * FONT_WIDTH, INPUT_ROW * FONT_HEIGHT,
                    under, COLOR_INPUT_BG, COLOR_CURSOR);
    }
    xSemaphoreGive(s_display_mutex);
}

void hal_display_append_conv(const char *text, uint16_t color)
{
    if (!text || !*text) return;

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);

    // Word-wrap text into scroll buffer lines
    const char *p = text;
    while (*p) {
        char *line = s_conv_buf[s_scroll_head];
        memset(line, 0, SCREEN_COLS + 1);
        s_conv_colors[s_scroll_head] = color;

        int col = 0;
        while (*p && *p != '\n' && col < SCREEN_COLS) {
            line[col++] = *p++;
        }
        if (*p == '\n') p++;

        s_scroll_head = (s_scroll_head + 1) % SCROLL_BUF_LINES;
        if (s_scroll_count < SCROLL_BUF_LINES) s_scroll_count++;
    }

    // Auto-scroll to bottom on new content
    s_view_offset = 0;
    refresh_conversation();
    xSemaphoreGive(s_display_mutex);
}
