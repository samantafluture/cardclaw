# CardClaw — Product Requirements Document (PRD)

> Version: 0.1
> Status: Draft
> Last updated: March 2026

---

## 1. Overview

### 1.1 What Is CardClaw?

CardClaw is an open-source AI agent firmware for the M5Stack Cardputer ADV (ESP32-S3). It is a fork of zclaw — the ultra-lightweight OpenClaw-ecosystem AI assistant — extended with native hardware UI support (keyboard, display, sensors) and integrated security/hacking tools.

CardClaw is the first *claw variant with a physical interface. Users type on a real keyboard, read responses on a real screen, and interact with the physical world through WiFi scanning, BLE reconnaissance, IR capture, and IMU gestures — all orchestrated by an AI agent that reasons about the results in natural language.

### 1.2 Problem Statement

The OpenClaw ecosystem has produced several embedded AI agents (MimiClaw, zclaw, PycoClaw), but all of them are headless — they communicate exclusively through Telegram, web relays, or serial CLI. There is no *claw variant that provides a native, self-contained user experience on a physical device.

Meanwhile, the M5Stack Cardputer community has a thriving ecosystem of security tools (Bruce, Evil-M5, Marauder) and utility firmwares, but none of them integrate AI-assisted reasoning. Users must manually interpret scan results, protocol data, and network information.

CardClaw bridges both gaps: an AI agent you can hold, type into, and point at the world.

### 1.3 Target Users

**Primary:** Makers, hackers, and tinkerers who own or plan to buy a Cardputer ADV and want an AI-augmented pocket tool for security learning, IoT experimentation, and general-purpose assistance.

**Secondary:** The broader ESP32/Cardputer open-source community — contributors who want to extend the tool system, add new hardware integrations, or port to other ESP32 devices.

**Tertiary:** Educators and students using the Cardputer for embedded systems, security, or AI/ML coursework.

### 1.4 Design Principles

1. **Physical-first.** The keyboard and display are the primary interface. Telegram is secondary. The device must be fully usable without a phone.
2. **Thin client.** Intelligence lives in the cloud (Anthropic, OpenAI, OpenRouter). The device is a sensor platform, input device, and display terminal. No on-device LLM inference.
3. **Tool-augmented.** Security capabilities (WiFi scan, BLE scan, IR, GPIO) are registered as tools the AI agent can invoke. The user describes intent; the agent orchestrates execution.
4. **Minimal and auditable.** Firmware stays lean. Code is C on ESP-IDF. No bloated frameworks. Every KB matters on 8MB flash / 320KB RAM.
5. **Open and forkable.** MIT license. Clean architecture. Good docs. Easy for others to add tools, change personas, or port to new hardware.

---

## 2. Functional Requirements

### 2.1 Core Agent

| ID | Requirement | Priority | Source |
|---|---|---|---|
| CA-01 | Agent loop implements ReAct reasoning pattern (observe → think → act → repeat) | P0 | zclaw upstream |
| CA-02 | Supports Anthropic (Claude), OpenAI, and OpenRouter as LLM providers, switchable at runtime | P0 | zclaw upstream |
| CA-03 | Tool use protocol: agent can call registered tools, receive results, and continue reasoning | P0 | zclaw upstream |
| CA-04 | Persistent memory across reboots (MEMORY.md equivalent stored in NVS or filesystem) | P0 | zclaw upstream |
| CA-05 | Personality configuration via SOUL.md file (loadable from microSD or flash) | P0 | zclaw upstream |
| CA-06 | User preferences via USER.md file | P1 | zclaw upstream |
| CA-07 | Conversation session management (list, clear, export) | P1 | zclaw upstream |
| CA-08 | Scheduled tasks with timezone awareness (one-shot, recurring, daily) | P1 | zclaw upstream |
| CA-09 | Custom tool composition: user defines up to 8 named tools via natural language | P2 | zclaw upstream |

### 2.2 Local Interface (Display + Keyboard)

| ID | Requirement | Priority |
|---|---|---|
| LI-01 | Terminal-style display: fixed-width font, character grid, auto-scrolling conversation view | P0 |
| LI-02 | Status bar (top of screen): WiFi signal indicator, battery %, active LLM provider/model | P0 |
| LI-03 | Input bar (bottom of screen): typed characters with blinking cursor | P0 |
| LI-04 | Full 56-key keyboard mapping including Fn, Ctrl, Alt, Opt modifiers | P0 |
| LI-05 | Key repeat and debounce handling | P0 |
| LI-06 | Basic line editing: backspace, clear line (Ctrl+U), clear screen (Ctrl+L) | P0 |
| LI-07 | Submit message on Enter key | P0 |
| LI-08 | Scroll back through conversation history (Fn+Up/Down) | P1 |
| LI-09 | Token-by-token response rendering (streaming display as LLM responds) | P1 |
| LI-10 | Typing/thinking indicator while waiting for LLM response | P1 |
| LI-11 | Auto-dim display after configurable inactivity period, wake on keypress | P2 |
| LI-12 | Display brightness adjustment via keyboard shortcut | P2 |

### 2.3 Communication Channels

| ID | Requirement | Priority |
|---|---|---|
| CC-01 | Local channel: keyboard input → agent loop → display output | P0 |
| CC-02 | Telegram channel: long-polling bot, bidirectional messaging | P0 |
| CC-03 | Serial CLI channel: USB console for debug, config, and direct agent interaction | P0 |
| CC-04 | All channels feed the same agent loop and share conversation context | P0 |
| CC-05 | Channel indicator on display showing message source (local / telegram / serial) | P2 |

### 2.4 Configuration & Setup

| ID | Requirement | Priority |
|---|---|---|
| CS-01 | On-device WiFi configuration: scan visible networks, select, enter password via keyboard | P0 |
| CS-02 | API key entry via keyboard (or load from config file on microSD) | P0 |
| CS-03 | All configuration persisted in NVS (survives reboots and reflash) | P0 |
| CS-04 | First-boot setup wizard guiding through WiFi → API key → Telegram token (optional) | P1 |
| CS-05 | Settings menu accessible via keyboard shortcut: WiFi, API provider, model, display, theme | P1 |
| CS-06 | Runtime config via serial CLI (matching zclaw's existing CLI commands) | P0 |
| CS-07 | Load SOUL.md, USER.md, MEMORY.md from microSD if present (override flash defaults) | P1 |
| CS-08 | Export conversation logs to microSD as timestamped markdown files | P2 |

### 2.5 Security Tools

| ID | Requirement | Priority |
|---|---|---|
| ST-01 | `wifi_scan` tool: scan nearby networks, return SSID, BSSID, channel, RSSI, encryption type | P1 |
| ST-02 | `ble_scan` tool: scan BLE advertising packets, return device name, MAC, RSSI, service UUIDs | P1 |
| ST-03 | `ir_capture` tool: record raw IR signal via built-in IR LED/receiver | P2 |
| ST-04 | `ir_replay` tool: replay a previously captured IR signal | P2 |
| ST-05 | `ir_identify` tool: attempt protocol identification from raw timing data | P2 |
| ST-06 | `net_probe` tool: check common ports on a given IP address | P2 |
| ST-07 | Scan results displayed in formatted table view on screen | P1 |
| ST-08 | Store captured signals/scan results on microSD as JSON | P2 |
| ST-09 | Agent can chain tools: "scan WiFi, find my network, check which devices have port 80 open" | P1 |

### 2.6 Sensors & Hardware

| ID | Requirement | Priority |
|---|---|---|
| SH-01 | `imu_read` tool: return current accelerometer/gyroscope values from BMI270 | P2 |
| SH-02 | Shake gesture detection → cancel current LLM generation | P2 |
| SH-03 | Flip face-down detection → enter "do not disturb" mode (pause Telegram polling) | P3 |
| SH-04 | `gpio_read` / `gpio_write` tools for Grove port peripherals | P1 |
| SH-05 | Battery level reading via ADC, displayed in status bar | P1 |

### 2.7 Audio

| ID | Requirement | Priority |
|---|---|---|
| AU-01 | Confirmation tone on successful tool execution (short beep via speaker) | P2 |
| AU-02 | Push-to-talk voice input: record via MEMS mic, send to cloud STT (Whisper/Gemini/Deepgram), feed transcription into agent loop | P3 |
| AU-03 | Optional TTS: send agent text response to cloud TTS, play via speaker or 3.5mm jack | P3 |
| AU-04 | Audio alert on scheduled task trigger | P2 |

### 2.8 Extensibility

| ID | Requirement | Priority |
|---|---|---|
| EX-01 | Custom tool definitions loadable from microSD (JSON format: name, description, action) | P2 |
| EX-02 | Theme/color scheme loadable from microSD (INI or JSON) | P2 |
| EX-03 | SOUL.md persona templates bundled with firmware (security analyst, study buddy, ops assistant) | P2 |
| EX-04 | Plugin architecture for community-contributed tool modules (compile-time feature flags) | P3 |

---

## 3. Non-Functional Requirements

### 3.1 Performance

| ID | Requirement |
|---|---|
| NF-01 | Boot to ready state (WiFi connected, display showing prompt) in under 5 seconds |
| NF-02 | Keyboard input latency < 50ms from keypress to character on screen |
| NF-03 | Time from Enter to first LLM response token displayed: dominated by network + LLM latency, not firmware overhead |
| NF-04 | WiFi scan completes in under 5 seconds |
| NF-05 | BLE scan returns results within 10 seconds |

### 3.2 Reliability

| ID | Requirement |
|---|---|
| NF-06 | Firmware must not crash on WiFi disconnection — gracefully retry and show status |
| NF-07 | API errors (rate limit, timeout, invalid key) display user-friendly message, do not crash |
| NF-08 | NVS corruption recovery: factory reset option via boot button hold |
| NF-09 | Watchdog timer prevents infinite loops in agent loop |

### 3.3 Resource Constraints

| ID | Requirement |
|---|---|
| NF-10 | Total firmware binary ≤ 2MB (target: ~1MB) |
| NF-11 | Runtime RAM usage stays within 320KB SRAM budget (no PSRAM available) |
| NF-12 | Display framebuffer: use partial updates or line-by-line rendering to minimize RAM |
| NF-13 | HTTP response buffers: stream and parse incrementally, never buffer full response in RAM |

### 3.4 Security

| ID | Requirement |
|---|---|
| NF-14 | All API communications over HTTPS/TLS |
| NF-15 | API keys stored in NVS with flash encryption enabled (if supported by board config) |
| NF-16 | No API keys in firmware binary or source code |
| NF-17 | WiFi credentials stored in NVS, not in plaintext config files |

### 3.5 Compatibility

| ID | Requirement |
|---|---|
| NF-18 | Primary target: M5Stack Cardputer ADV (ESP32-S3FN8) |
| NF-19 | Upstream zclaw compatibility: all zclaw serial CLI commands remain functional |
| NF-20 | Future portability: display/keyboard drivers abstracted behind HAL for potential porting to other devices (T-Deck, CYD) |

---

## 4. User Stories

### 4.1 First-Time Setup
> As a user who just flashed CardClaw, I want to configure WiFi and API keys directly on the device so I don't need a computer after the initial flash.

### 4.2 Basic Chat
> As a user, I want to type a question on the keyboard and see Claude's response on the screen, like a pocket ChatGPT terminal.

### 4.3 Security Scanning
> As a security learner, I want to say "scan my WiFi network and tell me what you find" and have the agent run a WiFi scan, analyze the results, and explain them to me in plain language.

### 4.4 IR Learning
> As a maker, I want to say "capture the IR signal from my TV remote and save it" and have the agent record, identify the protocol, and store it — all through conversation.

### 4.5 Remote Access
> As a user away from my Cardputer, I want to message the same agent via Telegram and get the same context-aware responses, including being able to trigger device tools remotely.

### 4.6 Persona Switching
> As a user, I want to swap SOUL.md files on my microSD to switch the agent's personality between "security analyst" and "study buddy" without reflashing.

### 4.7 Tool Chaining
> As a power user, I want to tell the agent "every hour, scan for new BLE devices near my desk and log them to the SD card" and have it set up a recurring schedule with tool execution.

---

## 5. Out of Scope (v1)

- On-device LLM inference (hardware cannot support it)
- Full offensive security suite (CardClaw is not a Bruce replacement)
- Multi-user support or user authentication
- Android/iOS companion app
- OTA firmware updates (deferred to v2)
- Camera or vision capabilities (no camera on Cardputer ADV)
- Sub-GHz RF tools (requires external CC1101 module — deferred to v2)
- RFID/NFC tools (requires external PN532 module — deferred to v2)

---

## 6. Release Milestones

### v0.1 — "It Talks"
- zclaw core running on Cardputer ADV
- Display driver + keyboard driver functional
- Local channel: type → agent → display
- Telegram channel working in parallel
- Serial CLI working
- Basic configuration via serial CLI

### v0.2 — "It Stands Alone"
- On-device WiFi setup wizard
- On-device API key configuration
- Menu system (Chat, Settings, Memory, About)
- microSD config loading (SOUL.md, USER.md, MEMORY.md)
- Battery indicator in status bar
- Conversation log export to microSD

### v0.3 — "It Sees"
- WiFi scan tool registered and functional
- BLE scan tool registered and functional
- GPIO read/write tools
- Formatted scan result display
- Agent can chain scan → analyze → explain

### v0.4 — "It Hears"
- IR capture/replay/identify tools
- IMU gesture detection (shake to cancel)
- Audio confirmation tones
- Push-to-talk voice input (cloud STT)

### v1.0 — "It Ships"
- Web flasher for browser-based installation
- Pre-compiled binaries on GitHub Releases
- Full documentation (README, ARCHITECTURE.md, wiki)
- Theme support
- Custom tool definitions from microSD
- M5Burner listing
- Community-ready (Discord, issue templates, contributing guide)

---

## 7. Competitive Landscape

| Project | Platform | UI | AI Agent | Security Tools | License |
|---|---|---|---|---|---|
| **CardClaw** | Cardputer ADV (ESP32-S3, 8MB, no PSRAM) | Keyboard + display | Yes (zclaw-based ReAct) | Yes (WiFi, BLE, IR) | MIT |
| zclaw | Any ESP32 | Headless (Telegram/serial) | Yes (ReAct) | No | MIT |
| MimiClaw | ESP32-S3 (16MB + 8MB PSRAM) | Headless (Telegram/WebSocket) | Yes (ReAct) | No | MIT |
| PycoClaw | ESP32 (MicroPython) | LVGL touchscreen | Yes (ReAct) | No | MIT |
| Bruce | Cardputer / ESP32 | Menu-driven display | No | Yes (full offensive suite) | AGPL |
| Evil-M5 | Cardputer / ESP32 | Menu-driven display | No | Yes (WiFi focused) | Open source |

**CardClaw's unique position:** Only project combining an AI agent with security tools AND a native physical keyboard/display interface.

---

*End of PRD*
