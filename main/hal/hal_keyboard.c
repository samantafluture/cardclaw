#include "cardclaw_hal.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "hal_keyboard";

// ---------------------------------------------------------------------------
// Cardputer ADV pin assignments (TCA8418)
// ---------------------------------------------------------------------------
#define PIN_SDA     GPIO_NUM_13
#define PIN_SCL     GPIO_NUM_15
#define PIN_INT     GPIO_NUM_10
#define I2C_PORT    I2C_NUM_0
#define I2C_FREQ    400000      // 400 kHz
#define TCA8418_ADDR 0x34       // 7-bit I2C address

// ---------------------------------------------------------------------------
// TCA8418 registers
// ---------------------------------------------------------------------------
#define REG_CFG             0x01
#define REG_INT_STAT        0x02
#define REG_KEY_LCK_EC      0x03
#define REG_KEY_EVENT_A     0x04
#define REG_KP_GPIO1        0x1D
#define REG_KP_GPIO2        0x1E
#define REG_KP_GPIO3        0x1F
#define REG_GPI_EM1         0x20
#define REG_GPI_EM2         0x21
#define REG_GPI_EM3         0x22
#define REG_GPIO_INT_EN1    0x1A
#define REG_GPIO_INT_EN2    0x1B
#define REG_GPIO_INT_EN3    0x1C
#define REG_DEBOUNCE_DIS1   0x29
#define REG_DEBOUNCE_DIS2   0x2A
#define REG_DEBOUNCE_DIS3   0x2B

// CFG register bits
#define CFG_AI              (1 << 7)  // Auto-increment
#define CFG_GPI_IEN         (1 << 1)  // GPI interrupt enable
#define CFG_KE_IEN          (1 << 0)  // Key event interrupt enable

// INT_STAT bits
#define INT_K_INT           (1 << 0)  // Key event interrupt

// Key event register bits
#define KEY_EVENT_PRESS     0x80
#define KEY_EVENT_CODE_MASK 0x7F

// ---------------------------------------------------------------------------
// Keymap: TCA8418 keycode → ASCII
// ---------------------------------------------------------------------------
// The Cardputer ADV has a 56-key matrix (4 rows × 14 columns).
// Keycode = (row * 14) + col + 1 (TCA8418 keycodes start at 1).
// This map covers keycodes 1-56 for the base (unshifted) layer.
// ---------------------------------------------------------------------------

#define KEYMAP_SIZE 57  // keycodes 0-56 (0 is unused)

// Base layer (no modifiers)
static const char s_keymap_base[KEYMAP_SIZE] = {
    [0]  = 0,
    // Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = BS
    [1]  = '`', [2]  = '1', [3]  = '2', [4]  = '3', [5]  = '4',
    [6]  = '5', [7]  = '6', [8]  = '7', [9]  = '8', [10] = '9',
    [11] = '0', [12] = '-', [13] = '=', [14] = '\b',
    // Row 1: Tab q w e r t y u i o p [ ] (Enter)
    [15] = '\t', [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r',
    [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o',
    [25] = 'p', [26] = '[', [27] = ']', [28] = '\n',
    // Row 2: (Ctrl) a s d f g h j k l ; ' (Fn)
    [29] = 0, [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f',
    [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k', [38] = 'l',
    [39] = ';', [40] = '\'', [41] = 0,
    // Row 3: (Shift) z x c v b n m , . / Space (Alt) (Opt)
    [42] = 0, [43] = 'z', [44] = 'x', [45] = 'c', [46] = 'v',
    [47] = 'b', [48] = 'n', [49] = 'm', [50] = ',', [51] = '.',
    [52] = '/', [53] = ' ', [54] = 0, [55] = 0, [56] = 0,
};

// Shift layer
static const char s_keymap_shift[KEYMAP_SIZE] = {
    [0]  = 0,
    [1]  = '~', [2]  = '!', [3]  = '@', [4]  = '#', [5]  = '$',
    [6]  = '%', [7]  = '^', [8]  = '&', [9]  = '*', [10] = '(',
    [11] = ')', [12] = '_', [13] = '+', [14] = '\b',
    [15] = '\t', [16] = 'Q', [17] = 'W', [18] = 'E', [19] = 'R',
    [20] = 'T', [21] = 'Y', [22] = 'U', [23] = 'I', [24] = 'O',
    [25] = 'P', [26] = '{', [27] = '}', [28] = '\n',
    [29] = 0, [30] = 'A', [31] = 'S', [32] = 'D', [33] = 'F',
    [34] = 'G', [35] = 'H', [36] = 'J', [37] = 'K', [38] = 'L',
    [39] = ':', [40] = '"', [41] = 0,
    [42] = 0, [43] = 'Z', [44] = 'X', [45] = 'C', [46] = 'V',
    [47] = 'B', [48] = 'N', [49] = 'M', [50] = '<', [51] = '>',
    [52] = '?', [53] = ' ', [54] = 0, [55] = 0, [56] = 0,
};

// Modifier keycodes
#define KEYCODE_CTRL    29
#define KEYCODE_FN      41
#define KEYCODE_SHIFT   42
#define KEYCODE_ALT     54
#define KEYCODE_OPT     55

// Fn+key → special action keycodes
#define KEYCODE_UP      23  // 'i' position → Fn+Up
#define KEYCODE_DOWN    37  // 'k' position → Fn+Down

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static QueueHandle_t s_event_queue;
static uint8_t s_modifiers;     // bitmask of active modifiers

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------

static esp_err_t tca_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(I2C_PORT, TCA8418_ADDR,
                                       buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t tca_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(I2C_PORT, TCA8418_ADDR,
                                         &reg, 1, val, 1,
                                         pdMS_TO_TICKS(100));
}

// ---------------------------------------------------------------------------
// Key event processing
// ---------------------------------------------------------------------------

static void process_key_event(uint8_t raw)
{
    bool pressed = (raw & KEY_EVENT_PRESS) != 0;
    uint8_t keycode = raw & KEY_EVENT_CODE_MASK;

    if (keycode == 0 || keycode >= KEYMAP_SIZE) return;

    // Update modifier state
    if (keycode == KEYCODE_CTRL) {
        if (pressed) s_modifiers |= MOD_CTRL; else s_modifiers &= ~MOD_CTRL;
        return;
    }
    if (keycode == KEYCODE_FN) {
        if (pressed) s_modifiers |= MOD_FN; else s_modifiers &= ~MOD_FN;
        return;
    }
    if (keycode == KEYCODE_SHIFT) {
        if (pressed) s_modifiers |= MOD_SHIFT; else s_modifiers &= ~MOD_SHIFT;
        return;
    }
    if (keycode == KEYCODE_ALT) {
        if (pressed) s_modifiers |= MOD_ALT; else s_modifiers &= ~MOD_ALT;
        return;
    }
    if (keycode == KEYCODE_OPT) {
        if (pressed) s_modifiers |= MOD_OPT; else s_modifiers &= ~MOD_OPT;
        return;
    }

    // Only process key presses, not releases
    if (!pressed) return;

    key_event_t evt = { .type = KEY_EVENT_CHAR, .ch = 0 };

    // Fn combinations
    if (s_modifiers & MOD_FN) {
        if (keycode == KEYCODE_UP) {
            evt.type = KEY_EVENT_FN_UP;
        } else if (keycode == KEYCODE_DOWN) {
            evt.type = KEY_EVENT_FN_DOWN;
        } else if (keycode == 47) {  // 'b'
            evt.type = KEY_EVENT_FN_B;
        } else if (keycode == 17) {  // 'w'
            evt.type = KEY_EVENT_FN_W;
        } else if (keycode == 49) {  // 'm'
            evt.type = KEY_EVENT_FN_M;
        } else if (keycode == 31) {  // 's'
            evt.type = KEY_EVENT_FN_S;
        } else {
            return;  // Unknown Fn combo, ignore
        }
        xQueueSend(s_event_queue, &evt, 0);
        return;
    }

    // Ctrl combinations
    if (s_modifiers & MOD_CTRL) {
        char base = s_keymap_base[keycode];
        if (base == 'u' || base == 'U') {
            evt.type = KEY_EVENT_CTRL_U;
        } else if (base == 'l' || base == 'L') {
            evt.type = KEY_EVENT_CTRL_L;
        } else if (base == 'c' || base == 'C') {
            evt.type = KEY_EVENT_CTRL_C;
        } else {
            return;
        }
        xQueueSend(s_event_queue, &evt, 0);
        return;
    }

    // Regular keys
    const char *map = (s_modifiers & MOD_SHIFT) ? s_keymap_shift : s_keymap_base;
    char ch = map[keycode];

    if (ch == '\n') {
        evt.type = KEY_EVENT_ENTER;
    } else if (ch == '\b') {
        evt.type = KEY_EVENT_BACKSPACE;
    } else if (ch >= 0x20 && ch <= 0x7E) {
        evt.type = KEY_EVENT_CHAR;
        evt.ch = ch;
    } else {
        return;  // Non-printable, ignore
    }

    xQueueSend(s_event_queue, &evt, 0);
}

// ---------------------------------------------------------------------------
// Interrupt handler + polling task
// ---------------------------------------------------------------------------

static volatile bool s_int_pending = false;

static void IRAM_ATTR keyboard_isr(void *arg)
{
    (void)arg;
    s_int_pending = true;
}

static void keyboard_poll_task(void *arg)
{
    (void)arg;

    while (1) {
        // Wait for interrupt or poll periodically (for key repeat)
        if (s_int_pending || true) {  // always check — covers edge cases
            s_int_pending = false;

            // Read and process all pending key events from FIFO
            uint8_t key_count;
            tca_read_reg(REG_KEY_LCK_EC, &key_count);
            key_count &= 0x0F;  // lower nibble = event count

            for (int i = 0; i < key_count; i++) {
                uint8_t event;
                if (tca_read_reg(REG_KEY_EVENT_A, &event) == ESP_OK) {
                    process_key_event(event);
                }
            }

            // Clear interrupt
            uint8_t int_stat;
            tca_read_reg(REG_INT_STAT, &int_stat);
            if (int_stat & INT_K_INT) {
                tca_write_reg(REG_INT_STAT, INT_K_INT);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms poll interval
    }
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

esp_err_t hal_keyboard_init(void)
{
    s_event_queue = xQueueCreate(16, sizeof(key_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;

    s_modifiers = 0;

    // Initialize I2C bus
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &i2c_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Configure TCA8418
    // Set all rows and columns as keypad scan lines
    tca_write_reg(REG_KP_GPIO1, 0xFF);  // Rows 0-7 as keypad
    tca_write_reg(REG_KP_GPIO2, 0xFF);  // Cols 0-7 as keypad
    tca_write_reg(REG_KP_GPIO3, 0x3F);  // Cols 8-13 as keypad

    // Enable key event interrupt
    tca_write_reg(REG_CFG, CFG_KE_IEN | CFG_AI);

    // Drain any pending events
    uint8_t key_count;
    tca_read_reg(REG_KEY_LCK_EC, &key_count);
    key_count &= 0x0F;
    for (int i = 0; i < key_count; i++) {
        uint8_t dummy;
        tca_read_reg(REG_KEY_EVENT_A, &dummy);
    }
    // Clear interrupts
    tca_write_reg(REG_INT_STAT, 0x0F);

    // Configure INT pin as input with interrupt
    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << PIN_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // TCA8418 INT is active-low
    };
    gpio_config(&int_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_INT, keyboard_isr, NULL);

    // Start polling task
    xTaskCreate(keyboard_poll_task, "kb_poll", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Keyboard initialized (TCA8418 at 0x%02X)", TCA8418_ADDR);
    return ESP_OK;
}

bool hal_keyboard_event_available(void)
{
    return uxQueueMessagesWaiting(s_event_queue) > 0;
}

key_event_t hal_keyboard_read_event(void)
{
    key_event_t evt = { .type = KEY_EVENT_CHAR, .ch = 0 };
    xQueueReceive(s_event_queue, &evt, portMAX_DELAY);
    return evt;
}
