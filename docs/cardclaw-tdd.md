# CardClaw — Technical Design Document (TDD)

> Version: 0.1
> Status: Draft
> Last updated: March 2026
> Companion to: CardClaw PRD v0.1

---

## 1. System Architecture

### 1.1 High-Level Overview

CardClaw is a bare-metal C firmware running on ESP-IDF / FreeRTOS. It extends zclaw's thin-client AI agent architecture with hardware drivers for the Cardputer ADV's display, keyboard, IMU, IR, and audio subsystems. The firmware is organized as a set of FreeRTOS tasks distributed across the ESP32-S3's two CPU cores.

```
┌──────────────────────────────────────────────────────┐
│                   CardClaw Firmware                   │
├────────────────────────┬─────────────────────────────┤
│       Core 0 (I/O)     │      Core 1 (Agent)         │
│                        │                              │
│  ┌──────────────────┐  │  ┌────────────────────────┐  │
│  │  WiFi Manager    │  │  │  Agent Loop (ReAct)    │  │
│  │  (events, scan)  │  │  │  - context builder     │  │
│  └──────────────────┘  │  │  - LLM HTTP client     │  │
│  ┌──────────────────┐  │  │  - tool executor       │  │
│  │  Telegram Poller │──┼─>│  - response formatter  │  │
│  └──────────────────┘  │  └──────────┬─────────────┘  │
│  ┌──────────────────┐  │             │                │
│  │  Serial CLI      │──┼─>           │                │
│  └──────────────────┘  │             │                │
│  ┌──────────────────┐  │             │                │
│  │  Keyboard Task   │──┼─> inbound   │                │
│  │  (TCA8418 I2C)   │  │  queue      │                │
│  └──────────────────┘  │      ┌──────┘                │
│  ┌──────────────────┐  │      v                       │
│  │  Display Task    │<─┼── outbound                   │
│  │  (ST7789V2 SPI)  │  │  queue                       │
│  └──────────────────┘  │                              │
│  ┌──────────────────┐  │  ┌────────────────────────┐  │
│  │  IMU Task        │  │  │  Tool Registry         │  │
│  │  (BMI270 I2C)    │──┼─>│  - wifi_scan           │  │
│  └──────────────────┘  │  │  - ble_scan            │  │
│  ┌──────────────────┐  │  │  - ir_capture/replay   │  │
│  │  Audio Task      │  │  │  - gpio_read/write     │  │
│  │  (ES8311 I2S)    │<─┼──│  - imu_read            │  │
│  └──────────────────┘  │  │  - memory_read/write   │  │
│                        │  │  - schedule             │  │
│                        │  │  - get_time             │  │
│                        │  └────────────────────────┘  │
├────────────────────────┴─────────────────────────────┤
│                     Storage Layer                     │
│  NVS (config, keys)  │  LittleFS (memory files)      │
│  microSD (logs, SOUL, signals, custom tools)          │
└──────────────────────────────────────────────────────┘
```

### 1.2 Core Separation Strategy

The ESP32-S3 has two Xtensa LX7 cores. CardClaw follows zclaw's core assignment pattern:

**Core 0 — I/O Core:** All blocking or latency-sensitive operations. WiFi event handling, Telegram HTTP long-polling, serial CLI REPL, keyboard scanning, display rendering, IMU polling, audio I/O. These tasks must not block each other, so they run as separate FreeRTOS tasks with appropriate priorities.

**Core 1 — Agent Core:** The ReAct agent loop exclusively. This core handles JSON construction, HTTP requests to the LLM API, tool execution dispatch, context building, and response parsing. Isolating the agent on its own core prevents network I/O stalls from delaying AI reasoning, and vice versa.

### 1.3 Message Flow

```
[Keyboard] ──> ┐
[Telegram] ──> ├──> inbound_queue (xQueueHandle) ──> [Agent Loop] ──> outbound_queue ──> [Display]
[Serial]   ──> ┘                                                                    ──> [Telegram]
                                                                                    ──> [Serial]
```

All input channels produce a `cardclaw_message_t` struct:

```c
typedef enum {
    CHANNEL_LOCAL,
    CHANNEL_TELEGRAM,
    CHANNEL_SERIAL
} cardclaw_channel_t;

typedef struct {
    cardclaw_channel_t source;
    char text[MAX_MESSAGE_LEN];    // 512 bytes max input
    int64_t timestamp;
    int64_t chat_id;               // Telegram only, 0 for local/serial
} cardclaw_message_t;
```

The agent loop processes messages identically regardless of source. Responses are routed back to the originating channel (and optionally mirrored to the display).

---

## 2. Hardware Abstraction Layer (HAL)

All hardware drivers are abstracted behind a HAL to enable future porting to other ESP32 devices (T-Deck, CYD, etc.).

### 2.1 HAL Interface

```c
// hal/cardclaw_hal.h

// Display
esp_err_t hal_display_init(void);
void hal_display_clear(void);
void hal_display_set_cursor(uint8_t col, uint8_t row);
void hal_display_print(const char *text);
void hal_display_print_line(uint8_t row, const char *text);
void hal_display_scroll_up(uint8_t lines);
void hal_display_set_brightness(uint8_t percent);
void hal_display_status_bar(const char *left, const char *right);

// Keyboard
esp_err_t hal_keyboard_init(void);
bool hal_keyboard_available(void);
char hal_keyboard_read(void);           // blocking
uint8_t hal_keyboard_read_raw(void);    // raw keycode

// IMU
esp_err_t hal_imu_init(void);
void hal_imu_read(float *ax, float *ay, float *az, float *gx, float *gy, float *gz);
bool hal_imu_shake_detected(void);

// IR
esp_err_t hal_ir_init(void);
esp_err_t hal_ir_send(const uint8_t *data, size_t len, uint16_t protocol);
esp_err_t hal_ir_receive(uint8_t *buf, size_t *len, uint32_t timeout_ms);

// Audio
esp_err_t hal_audio_init(void);
void hal_audio_beep(uint16_t freq_hz, uint16_t duration_ms);
esp_err_t hal_audio_record(uint8_t *buf, size_t len, uint32_t sample_rate);
esp_err_t hal_audio_play(const uint8_t *buf, size_t len, uint32_t sample_rate);

// Storage
esp_err_t hal_sd_init(void);
bool hal_sd_mounted(void);

// Power
uint8_t hal_battery_percent(void);
```

### 2.2 Cardputer ADV Pin Map

| Peripheral | Interface | Pins |
|---|---|---|
| Display (ST7789V2) | SPI | CS=GPIO7, DC=GPIO34, RST=GPIO33, SCLK=GPIO36, MOSI=GPIO35, BL=GPIO38 |
| Keyboard (TCA8418) | I2C | SDA=GPIO13, SCL=GPIO15, INT=GPIO10 |
| IMU (BMI270) | I2C | Shared bus with keyboard (SDA=GPIO13, SCL=GPIO15) |
| Audio (ES8311) | I2S + I2C | I2S_BCLK=GPIO41, I2S_WS=GPIO43, I2S_DOUT=GPIO42, I2S_DIN=GPIO44, I2C ctrl on shared bus |
| IR LED | GPIO | GPIO44 (shared — verify mux with audio) |
| microSD | SPI | CS=GPIO12, shared SPI bus with display |
| Battery ADC | ADC | GPIO10 (verify — may share with keyboard INT) |
| Grove Port | I2C / GPIO | HY2.0-4P connector, GPIO1/GPIO2 |

**Note:** Pin assignments must be verified against the official Cardputer ADV schematic. Some pins may be multiplexed. The HAL abstracts these details so pin conflicts are resolved in one place.

---

## 3. Module Design

### 3.1 Display Module (`display/`)

**Driver:** ST7789V2 via SPI at 40MHz.

**Rendering strategy:** No full framebuffer (240×135×16bpp = ~64KB would consume 20% of available RAM). Instead, use line-by-line rendering:

- Maintain a text buffer: `char screen_buf[SCREEN_ROWS][SCREEN_COLS]` where `SCREEN_ROWS` ≈ 11 and `SCREEN_COLS` ≈ 26 (at 8×12 font on 240×135 display, reserving top row for status bar and bottom row for input).
- On text change, re-render only the affected lines by pushing pixel data row-by-row via SPI DMA.
- Font: Embedded 8×12 monospace bitmap font (~3KB for full ASCII). No external font files.

**Screen layout:**

```
┌──────────────────────────┐
│ ⚡95%  WiFi  claude-haiku │  ← Status bar (row 0)
├──────────────────────────┤
│ > what networks are near │  ← Conversation area
│ me?                      │    (rows 1-9, scrollable)
│                          │
│ I found 4 WiFi networks: │
│ 1. HomeNet (-42dBm, WPA2)│
│ 2. Guest (-65dBm, Open)  │
│ 3. IoT_Device (-71dBm,   │
│    WPA2)                 │
│ 4. Neighbor (-80dBm,     │
├──────────────────────────┤
│ > scan for bluetooth_    │  ← Input bar (row 10)
└──────────────────────────┘
```

**Scroll buffer:** Keep the last 100 lines in a circular buffer (`char scroll_buf[100][SCREEN_COLS]`). Fn+Up/Down moves a viewport window over this buffer. Cost: ~2.6KB.

**Color scheme (defaults, overridable via theme file):**

```c
#define COLOR_BG        0x0000   // Black
#define COLOR_TEXT       0x07E0   // Green (terminal aesthetic)
#define COLOR_STATUS_BG  0x18E3  // Dark gray
#define COLOR_STATUS_FG  0xFFFF  // White
#define COLOR_INPUT_BG   0x0841  // Very dark gray
#define COLOR_CURSOR     0x07E0  // Green, blinking
#define COLOR_USER_MSG   0x07E0  // Green
#define COLOR_AGENT_MSG  0xFFFF  // White
#define COLOR_ERROR      0xF800  // Red
#define COLOR_TOOL_MSG   0xFD20  // Orange
```

### 3.2 Keyboard Module (`keyboard/`)

**Driver:** TCA8418 via I2C at 400kHz (shared bus with BMI270 and ES8311 control).

The TCA8418 is a hardware key-scan engine that handles the 4×14 matrix autonomously and raises an interrupt on keypress. The driver:

1. Configures the TCA8418 registers on init (rows, columns, interrupt enable)
2. On GPIO interrupt (INT pin), reads the key event FIFO via I2C
3. Translates raw keycodes to ASCII using a keymap table
4. Handles modifier state (Fn, Shift/Aa, Ctrl, Alt, Opt) as a bitmask
5. Pushes translated characters into a line buffer

**Line buffer:**

```c
typedef struct {
    char buf[MAX_INPUT_LEN];    // 256 chars max
    uint16_t cursor;
    uint16_t len;
} line_buffer_t;
```

**Key repeat:** Software-implemented. After initial keypress, wait 500ms, then repeat at 50ms intervals while held. Detected by polling the TCA8418 key-state register.

**Keyboard shortcuts (active globally):**

| Shortcut | Action |
|---|---|
| Enter | Submit message to agent |
| Backspace | Delete character before cursor |
| Ctrl+U | Clear input line |
| Ctrl+L | Clear screen (keep conversation in scroll buffer) |
| Ctrl+C | Cancel current LLM generation |
| Fn+Up/Down | Scroll conversation history |
| Fn+B | Adjust display brightness |
| Fn+W | Open WiFi settings |
| Fn+M | Open menu |
| Fn+S | Save conversation to microSD |

### 3.3 Agent Module (`agent/`)

**Inherited from zclaw with minimal modifications.**

**Agent loop (`agent_loop.c`):** Runs as a FreeRTOS task pinned to Core 1. Blocks on the inbound message queue. On message:

1. **Context build:** Load SOUL.md + USER.md + MEMORY.md + recent conversation history. Construct system prompt (max 16KB).
2. **LLM request:** POST to provider API (Anthropic/OpenAI/OpenRouter) with messages array and tool definitions.
3. **Response parse:** Stream-parse the HTTP response. For each text chunk, push to outbound queue for display. For each tool_use block, dispatch to tool executor.
4. **Tool execution:** Call the registered tool function, collect result, append to messages, loop back to step 2 (ReAct iteration).
5. **Completion:** When the LLM returns a final text response with no tool calls, mark the turn complete.

**Tool registry (`tool_registry.c`):**

```c
typedef struct {
    const char *name;
    const char *description;
    const char *input_schema;    // JSON schema string
    tool_handler_fn handler;     // function pointer
} cardclaw_tool_t;

#define MAX_TOOLS 16

// Registration
esp_err_t tool_register(const cardclaw_tool_t *tool);
const cardclaw_tool_t* tool_find(const char *name);
const char* tool_definitions_json(void);  // generates tools array for LLM API
```

**Built-in tools:**

| Tool Name | Description | Module |
|---|---|---|
| `get_current_time` | Returns current local time with timezone | zclaw core |
| `memory_read` | Read persistent memory (MEMORY.md) | zclaw core |
| `memory_write` | Append to persistent memory | zclaw core |
| `schedule_create` | Create a one-shot or recurring scheduled task | zclaw core |
| `schedule_list` | List active schedules | zclaw core |
| `schedule_delete` | Delete a schedule by ID | zclaw core |
| `gpio_read` | Read a GPIO pin value | zclaw core |
| `gpio_write` | Set a GPIO pin value | zclaw core |
| `wifi_scan` | Scan visible WiFi networks | CardClaw new |
| `ble_scan` | Scan BLE advertising packets | CardClaw new |
| `ir_capture` | Record raw IR signal | CardClaw new |
| `ir_replay` | Replay a saved IR signal | CardClaw new |
| `ir_identify` | Identify IR protocol from timing | CardClaw new |
| `net_probe` | Check open ports on a target IP | CardClaw new |
| `imu_read` | Read accelerometer/gyroscope | CardClaw new |
| `battery_status` | Return battery percentage | CardClaw new |

### 3.4 Storage Module (`storage/`)

**Three storage backends:**

**NVS (Non-Volatile Storage):** For key-value configuration. WiFi credentials, API keys, Telegram token, display settings, timezone. Small values only (max 4KB per entry). Encrypted if flash encryption is enabled.

**LittleFS (Internal Flash Partition):** For agent memory files. MEMORY.md, daily notes, session data. Partition size: ~2MB (configurable in partition table). Wear-leveled, power-loss safe.

**microSD (FAT32):** For user-editable files and logs. SOUL.md, USER.md, conversation exports, captured IR signals, scan results, custom tool definitions, themes. Optional — firmware works without SD card, falling back to LittleFS for all storage.

**File precedence:** microSD > LittleFS > compiled defaults. If SOUL.md exists on microSD, it overrides the version on LittleFS, which overrides the default personality compiled into firmware.

**Partition table (8MB flash):**

| Partition | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24KB |
| otadata | data/ota | 0xF000 | 8KB |
| ota_0 | app/ota_0 | 0x10000 | 2.5MB |
| ota_1 | app/ota_1 | — | (reserved for future OTA, 0 for v0.1) |
| littlefs | data/littlefs | 0x290000 | 2MB |
| coredump | data/coredump | 0x490000 | 64KB |

**Note:** Without OTA in v0.1, ota_1 space can be reclaimed for a larger LittleFS partition (~4.5MB). OTA support in v2 will require revisiting the partition layout.

### 3.5 Network Module (`network/`)

**WiFi manager (`wifi_manager.c`):** Handles STA mode connection, auto-reconnect with exponential backoff, and scan functionality. Events dispatched via ESP-IDF's event loop.

**HTTP client (`http_client.c`):** ESP-IDF's `esp_http_client` for all outbound HTTPS requests. Shared TLS session where possible to reduce handshake overhead.

**LLM client (`llm_client.c`):** Abstracts provider differences:

```c
typedef enum {
    LLM_PROVIDER_ANTHROPIC,
    LLM_PROVIDER_OPENAI,
    LLM_PROVIDER_OPENROUTER
} llm_provider_t;

typedef struct {
    llm_provider_t provider;
    const char *api_key;
    const char *model;
    const char *base_url;   // allows custom endpoints / proxies
} llm_config_t;

// Sends messages + tools to LLM, calls callback for each response chunk
esp_err_t llm_chat(const llm_config_t *config,
                   const char *system_prompt,
                   const char *messages_json,
                   const char *tools_json,
                   llm_stream_callback_t callback,
                   void *user_data);
```

**Streaming:** The LLM client uses chunked transfer encoding to parse SSE (Server-Sent Events) for Anthropic or streaming responses for OpenAI. Each text delta is forwarded to the display task via the outbound queue, enabling token-by-token rendering.

**Telegram client (`telegram_client.c`):** Long-polling via `getUpdates` with 30-second timeout. Handles message splitting for responses > 4096 characters. Inherited from zclaw with no modifications needed.

**Proxy support:** HTTP CONNECT tunnel support for restricted networks (inherited from zclaw/MimiClaw pattern).

### 3.6 Security Tools Module (`tools/security/`)

**WiFi scanner (`tool_wifi_scan.c`):**

```c
// Tool handler signature
char* tool_wifi_scan_handler(const char *input_json);

// Implementation:
// 1. Call esp_wifi_scan_start() in blocking mode
// 2. Retrieve results via esp_wifi_scan_get_ap_records()
// 3. Format as JSON array: [{ssid, bssid, channel, rssi, authmode}, ...]
// 4. Return JSON string (caller frees)
```

**BLE scanner (`tool_ble_scan.c`):**

```c
// Uses ESP-IDF's BLE GAP scanning API
// 1. Start BLE scan for configurable duration (default 5s)
// 2. Collect advertising reports in a callback
// 3. Deduplicate by MAC address
// 4. Format as JSON: [{name, mac, rssi, services[], manufacturer_data}, ...]
```

**IR tools (`tool_ir.c`):**

```c
// Capture: Use ESP-IDF's RMT (Remote Control) peripheral
// 1. Configure RMT receiver on IR GPIO
// 2. Record pulse train (mark/space timings) into buffer
// 3. Store as JSON: {protocol: "raw", pulses: [mark_us, space_us, ...]}
// 4. Save to microSD if requested

// Identify: Analyze pulse timings against known protocol templates
// - NEC: 9ms leader + 4.5ms space + 32-bit data
// - RC5: Manchester-encoded, 14 bits, 889µs bit time
// - Sony SIRC: 2.4ms leader + 600µs space + variable data bits

// Replay: Load signal from storage, configure RMT transmitter, send
```

**Net probe (`tool_net_probe.c`):**

```c
// 1. Parse target IP from input JSON
// 2. For each port in scan list (default: 22, 80, 443, 8080, 8443):
//    - Attempt TCP connect with 2-second timeout
//    - Record open/closed/timeout
// 3. Return JSON: {ip, ports: [{port, status, service_hint}, ...]}
```

---

## 4. Memory Budget

### 4.1 RAM Allocation Plan (320KB total usable)

| Component | Estimated RAM | Notes |
|---|---|---|
| FreeRTOS kernel + task stacks | ~40KB | 8 tasks × ~4KB stack each + kernel overhead |
| WiFi driver buffers | ~40KB | ESP-IDF WiFi requires significant buffer space |
| TLS session + cert storage | ~30KB | Single active TLS session |
| Display text buffer | ~3KB | 100-line scroll buffer + screen buffer |
| Keyboard line buffer + state | ~1KB | 256-char input + modifier state |
| Agent context builder | ~20KB | System prompt construction buffer |
| HTTP response parser | ~8KB | Streaming JSON parser buffer |
| Tool result buffers | ~4KB | Largest tool result (WiFi scan with 20 APs) |
| Inbound/outbound queues | ~4KB | 8 messages × 520 bytes each |
| NVS cache | ~4KB | ESP-IDF internal |
| LittleFS cache | ~8KB | Configurable cache size |
| BLE scan buffers | ~8KB | Advertising report storage (when active) |
| IR RMT buffers | ~4KB | Pulse train recording (when active) |
| General heap headroom | ~146KB | Available for dynamic allocations |
| **Total** | **~320KB** | |

**Key principle:** BLE scan buffers and IR buffers are allocated only when those tools are invoked and freed immediately after. They do not persist. This keeps the baseline RAM footprint low.

### 4.2 Flash Allocation Plan (8MB total)

| Region | Size | Contents |
|---|---|---|
| Bootloader | 28KB | ESP-IDF second-stage bootloader |
| Partition table | 4KB | Partition definitions |
| NVS | 24KB | Key-value config storage |
| Application (ota_0) | 2.5MB | CardClaw firmware binary (~1MB used, ~1.5MB headroom) |
| LittleFS | 2MB | Agent memory files, daily notes, session data |
| Reserved / coredump | 64KB | Crash diagnostics |
| Unallocated | ~3.3MB | Available for future OTA partition or larger LittleFS |

---

## 5. Build System

### 5.1 Toolchain

- **Framework:** ESP-IDF v5.5+ (not Arduino — zclaw upstream uses ESP-IDF)
- **Build system:** CMake via `idf.py`
- **Development:** PlatformIO optional (for IDE integration), but `idf.py` is the canonical build path
- **Target:** `esp32s3`
- **Compiler:** Xtensa GCC (bundled with ESP-IDF)

### 5.2 Project Structure

```
cardclaw/
├── CMakeLists.txt
├── sdkconfig.defaults              # Cardputer ADV-specific defaults
├── partitions.csv                   # Custom partition table
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                      # Entry point, task creation
│   ├── cardclaw_config.h           # Build-time defaults
│   ├── cardclaw_secrets.h.example  # Template for API keys
│   │
│   ├── hal/                        # Hardware abstraction
│   │   ├── hal.h                   # HAL interface
│   │   ├── hal_display_st7789.c    # Cardputer ADV display
│   │   ├── hal_keyboard_tca8418.c  # Cardputer ADV keyboard
│   │   ├── hal_imu_bmi270.c        # Cardputer ADV IMU
│   │   ├── hal_ir.c                # Cardputer ADV IR (RMT)
│   │   ├── hal_audio_es8311.c      # Cardputer ADV audio
│   │   └── hal_power.c             # Battery ADC
│   │
│   ├── ui/                         # User interface
│   │   ├── terminal.c              # Terminal renderer
│   │   ├── terminal.h
│   │   ├── status_bar.c            # Status bar renderer
│   │   ├── menu.c                  # Menu system
│   │   ├── setup_wizard.c          # First-boot configuration
│   │   ├── input.c                 # Line editing / input handling
│   │   └── font8x12.h             # Embedded bitmap font
│   │
│   ├── agent/                      # AI agent (zclaw-derived)
│   │   ├── agent_loop.c            # ReAct loop
│   │   ├── context_builder.c       # System prompt assembly
│   │   ├── tool_registry.c         # Tool registration + dispatch
│   │   └── tool_registry.h
│   │
│   ├── channels/                   # Communication channels
│   │   ├── channel.h               # Channel interface
│   │   ├── channel_local.c         # Keyboard → agent → display
│   │   ├── channel_telegram.c      # Telegram bot (zclaw-derived)
│   │   └── channel_serial.c        # USB serial CLI (zclaw-derived)
│   │
│   ├── network/                    # Networking
│   │   ├── wifi_manager.c          # WiFi STA + scan
│   │   ├── llm_client.c            # LLM API abstraction
│   │   ├── http_client.c           # HTTPS helpers
│   │   └── telegram_client.c       # Telegram API (zclaw-derived)
│   │
│   ├── tools/                      # Tool implementations
│   │   ├── tool_time.c             # get_current_time
│   │   ├── tool_memory.c           # memory_read / memory_write
│   │   ├── tool_schedule.c         # schedule CRUD
│   │   ├── tool_gpio.c             # gpio_read / gpio_write
│   │   ├── tool_wifi_scan.c        # WiFi scanner
│   │   ├── tool_ble_scan.c         # BLE scanner
│   │   ├── tool_ir.c               # IR capture / replay / identify
│   │   ├── tool_net_probe.c        # Port scanner
│   │   ├── tool_imu.c              # IMU data reader
│   │   └── tool_battery.c          # Battery status
│   │
│   ├── storage/                    # Persistence
│   │   ├── nvs_config.c            # NVS key-value operations
│   │   ├── filesystem.c            # LittleFS operations
│   │   └── sdcard.c                # microSD mount + file I/O
│   │
│   └── personas/                   # Bundled SOUL.md files
│       ├── default.md              # General-purpose assistant
│       ├── security_analyst.md     # Security-focused persona
│       └── study_buddy.md          # Learning-focused persona
│
├── components/                     # ESP-IDF components (external libs)
│   └── (any vendored C libraries)
│
├── sd_card_template/               # Default SD card contents
│   ├── SOUL.md
│   ├── USER.md
│   ├── tools/                      # Custom tool definitions
│   │   └── example_tool.json
│   └── themes/
│       └── default.ini
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── CONTRIBUTING.md
│   ├── PIN_MAP.md
│   └── TOOLS.md
│
├── scripts/
│   ├── flash.sh                    # Build + flash helper
│   ├── monitor.sh                  # Serial monitor helper
│   └── provision.sh                # WiFi + API key provisioning
│
├── LICENSE                         # MIT
└── README.md
```

### 5.3 Build Configuration (`sdkconfig.defaults`)

```ini
# Cardputer ADV specific
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=n

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=12

# Bluetooth (for BLE scan tool)
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=0

# TLS
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=8192

# Power
CONFIG_PM_ENABLE=y
CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND=y
```

---

## 6. FreeRTOS Task Layout

| Task | Core | Priority | Stack Size | Description |
|---|---|---|---|---|
| `agent_task` | 1 | 5 | 8KB | ReAct agent loop |
| `keyboard_task` | 0 | 10 | 4KB | TCA8418 interrupt handler + line editing |
| `display_task` | 0 | 8 | 4KB | Renders outbound queue to ST7789V2 |
| `telegram_task` | 0 | 3 | 6KB | Long-polling Telegram API |
| `serial_cli_task` | 0 | 2 | 4KB | USB serial REPL |
| `wifi_event_task` | 0 | 7 | 4KB | WiFi event handling + auto-reconnect |
| `imu_task` | 0 | 1 | 2KB | Gesture detection (low priority, periodic) |
| `scheduler_task` | 0 | 4 | 4KB | Cron-like scheduled task executor |

**Total stack allocation:** ~36KB

---

## 7. API Integration

### 7.1 Anthropic API

```
POST https://api.anthropic.com/v1/messages
Headers:
  x-api-key: <key>
  anthropic-version: 2023-06-01
  content-type: application/json

Body:
{
  "model": "claude-haiku-4-5-20251001",
  "max_tokens": 1024,
  "stream": true,
  "system": "<SOUL.md + USER.md + MEMORY.md + daily notes>",
  "tools": [<tool definitions>],
  "messages": [<conversation history>]
}
```

**Streaming:** Parse `event: content_block_delta` SSE events. Extract `delta.text` for display rendering. Extract `content_block_start` with `type: tool_use` for tool dispatch.

**Default model:** `claude-haiku-4-5-20251001` (fast, cheap, sufficient for tool orchestration). User can switch to Sonnet/Opus via settings.

### 7.2 OpenAI API

```
POST https://api.openai.com/v1/chat/completions
Headers:
  Authorization: Bearer <key>
  Content-Type: application/json

Body:
{
  "model": "gpt-4o-mini",
  "stream": true,
  "messages": [...],
  "tools": [...]
}
```

### 7.3 OpenRouter API

Same schema as OpenAI, different base URL (`https://openrouter.ai/api/v1`). Allows access to many models through a single API key.

---

## 8. Custom Tool Definition Format

Users can define custom tools by placing JSON files in `sd:/tools/` on the microSD card.

```json
{
  "name": "check_server",
  "description": "Check if my home server is reachable",
  "parameters": {
    "type": "object",
    "properties": {},
    "required": []
  },
  "action": {
    "type": "http_get",
    "url": "http://192.168.1.100:8080/health",
    "timeout_ms": 5000
  }
}
```

**Supported action types (v0.1):**

| Type | Description |
|---|---|
| `http_get` | Make an HTTP GET request, return body as tool result |
| `http_post` | Make an HTTP POST request with JSON body |
| `gpio_sequence` | Execute a sequence of GPIO writes with delays |
| `composite` | Chain multiple built-in tools in sequence |

---

## 9. Theme File Format

`sd:/themes/theme.ini`:

```ini
[colors]
bg=0x0000
text=0x07E0
status_bg=0x18E3
status_fg=0xFFFF
input_bg=0x0841
cursor=0x07E0
user_msg=0x07E0
agent_msg=0xFFFF
error=0xF800
tool_msg=0xFD20

[display]
brightness=80
auto_dim_seconds=60
font_size=1

[agent]
default_model=claude-haiku-4-5-20251001
default_provider=anthropic
```

---

## 10. Testing Strategy

### 10.1 Unit Tests

- Tool handlers tested on host (x86) using ESP-IDF's host-based test framework
- JSON serialization/deserialization tested with known inputs
- Keymap translation table tested exhaustively
- Context builder tested with various SOUL.md / MEMORY.md sizes

### 10.2 Integration Tests

- Full agent loop tested against a mock LLM server (returns canned responses)
- WiFi scan tool tested in a controlled RF environment
- Display rendering tested via screenshot capture (SPI bus sniffer or manual verification)

### 10.3 Hardware Validation

- Test matrix across Cardputer ADV hardware revisions (if multiple exist)
- Battery life benchmark: continuous chat session until shutdown
- Memory stress test: sustained conversation with tool calls, monitoring heap fragmentation
- WiFi reconnection test: disconnect/reconnect cycles with in-flight agent requests

---

## 11. Dependencies

| Dependency | Version | Purpose | License |
|---|---|---|---|
| ESP-IDF | v5.5+ | Framework, FreeRTOS, drivers | Apache 2.0 |
| cJSON | bundled with ESP-IDF | JSON parsing | MIT |
| LittleFS | ESP-IDF component | Flash filesystem | BSD |
| NimBLE | ESP-IDF component | BLE scanning | Apache 2.0 |
| zclaw (forked) | latest | Agent core, tools, scheduling | MIT |

**No external npm/pip dependencies.** Everything compiles from C source with ESP-IDF's CMake build system.

---

## 12. Contribution Guidelines (Summary)

- **Language:** C (C11 standard)
- **Style:** ESP-IDF coding style (4-space indent, snake_case, ESP_LOG macros)
- **Max file size:** 400 lines (signal to split)
- **Documentation:** Every public function has a JSDoc-style comment block
- **PRs:** Must include description, testing notes, and firmware size delta
- **Tools:** New tools must register via `tool_register()`, include input schema, and handle errors gracefully
- **HAL:** All hardware access goes through `hal/`. Direct GPIO/SPI/I2C calls outside HAL are rejected in review.

---

*End of TDD*
