# CardClaw — Project Roadmap

> The first OpenClaw-ecosystem AI agent with a native physical interface.
> Fork of zclaw, built for the M5Stack Cardputer ADV.

---

## Vision

CardClaw is an open-source AI agent firmware that runs on the M5Stack Cardputer ADV (ESP32-S3), giving users a pocket-sized, keyboard-driven AI assistant with built-in security tools. It is the first *claw variant with a native hardware UI — a real keyboard, a real screen, and physical sensors — bridging the gap between headless microcontroller agents and full desktop assistants.

The project lives at the intersection of three communities: the OpenClaw/zclaw embedded AI ecosystem, the M5Stack Cardputer hacking community, and the broader ESP32 maker scene.

---

## Target Hardware

- **Device:** M5Stack Cardputer ADV
- **MCU:** ESP32-S3FN8 (dual-core Xtensa LX7, 240MHz)
- **Flash:** 8MB (no PSRAM)
- **RAM:** ~320KB usable SRAM at runtime
- **Display:** 1.14" ST7789V2 IPS LCD (240×135px)
- **Keyboard:** 56-key QWERTY (TCA8418 I2C controller)
- **Audio:** ES8311 codec, MEMS mic, NS4150B amp, 1W speaker, 3.5mm jack
- **Sensors:** BMI270 6-axis IMU, IR LED
- **Connectivity:** WiFi (2.4GHz), BLE
- **Storage:** microSD slot, NVS flash
- **Battery:** 1750mAh LiPo
- **Expansion:** HY2.0-4P Grove port, EXT 2.54-14P header

---

## Why Fork zclaw (Not MimiClaw)

| Criteria | zclaw | MimiClaw |
|---|---|---|
| Firmware size | ~888KB (35KB app code) | Multi-MB |
| Flash required | Any ESP32 (4MB+) | 16MB |
| PSRAM required | No | 8MB |
| ESP32-S3 tested | Yes | Yes |
| License | MIT | MIT |
| Architecture | Thin client, cloud LLM | Thin client, cloud LLM |
| Codebase complexity | Minimal, focused | Larger, more features |
| Display/keyboard support | None | None |

**Verdict:** zclaw fits the Cardputer ADV's 8MB flash / no PSRAM constraints with room to spare. MimiClaw's hardware requirements are 2× what the Cardputer offers. Both are MIT-licensed, but zclaw's minimal codebase (~35KB app logic) is easier to extend without breaking the firmware budget.

---

## Firmware Size Budget

| Component | Estimated Size | Source |
|---|---|---|
| zclaw core (agent + tools + memory + scheduling) | ~35KB | Existing |
| ESP-IDF/FreeRTOS runtime | ~388KB | Existing |
| TLS/crypto + cert bundle | ~110KB | Existing |
| WiFi/networking stack | ~300KB | Existing |
| **Subtotal (zclaw baseline)** | **~833KB** | |
| ST7789V2 display driver (SPI) | ~15-20KB | New |
| TCA8418 keyboard driver (I2C) | ~5KB | New |
| Local terminal channel (input/output/scroll) | ~10-15KB | New |
| BMI270 IMU driver (I2C) | ~5KB | New |
| IR tools integration | ~5-8KB | New |
| WiFi scan tool | ~3-5KB | New |
| Status bar / UI chrome | ~5KB | New |
| **Total estimated** | **~900-920KB** | |
| **Available flash** | **~4MB** (after partitions) | |
| **Headroom** | **~3MB+** | Comfortable |

---

## Milestone Roadmap

### Phase 0 — Foundation

**Goal:** Fork zclaw, get it compiling and running on Cardputer ADV hardware.

- [ ] Fork zclaw repository
- [ ] Set up ESP-IDF v5.5+ development environment (WSL2)
- [ ] Create Cardputer ADV board configuration in `platformio.ini` / `sdkconfig`
- [ ] Configure flash partition table for 8MB (firmware + NVS + LittleFS/SPIFFS + OTA)
- [ ] Verify zclaw core compiles for ESP32-S3 target
- [ ] Flash and confirm WiFi connectivity, Telegram channel, serial CLI all work headlessly
- [ ] Document build process in README
- [ ] Set up GitHub repo with MIT license, CONTRIBUTING.md, issue templates

**Deliverable:** zclaw running on Cardputer ADV, headless mode, fully functional via Telegram and serial CLI.

---

### Phase 1 — Display & Keyboard

**Goal:** Add the local physical UI — the core differentiator from every other *claw project.

#### 1a. Display Driver
- [ ] Implement ST7789V2 SPI driver for ESP-IDF (or port from M5Unified/TFT_eSPI)
- [ ] Build a minimal terminal renderer: fixed-width font, character grid, auto-scroll
- [ ] Implement text wrapping for 240px width (~26-30 chars per line at a readable font size)
- [ ] Add a status bar (top): WiFi icon, battery %, model provider indicator
- [ ] Add an input bar (bottom): shows typed characters with cursor

#### 1b. Keyboard Driver
- [ ] Implement TCA8418 I2C driver for ESP-IDF
- [ ] Map all 56 keys including Fn, Ctrl, Alt, Opt modifiers
- [ ] Handle key repeat, debounce
- [ ] Pipe keyboard input into a line buffer with basic editing (backspace, clear line)
- [ ] On Enter: push completed message into zclaw's inbound message queue

#### 1c. Local Channel
- [ ] Create a "local" channel type alongside Telegram and serial
- [ ] Keyboard input → agent loop → display output
- [ ] Token-by-token rendering as the LLM response streams in (if zclaw supports SSE) or full response display
- [ ] Scroll back through conversation history with Fn+Up/Down
- [ ] All three channels (local, Telegram, serial) active simultaneously

**Deliverable:** Type a message on the Cardputer keyboard, see Claude's response on the screen. Telegram still works in parallel.

---

### Phase 2 — Configuration & UX

**Goal:** Make CardClaw usable without a computer after initial flash.

#### 2a. On-Device Setup Wizard
- [ ] First-boot WiFi configuration via keyboard (scan networks → select → type password)
- [ ] API key entry on-device (or load from `config.txt` on microSD)
- [ ] Telegram bot token entry on-device
- [ ] Store all config in NVS (persists across reboots)

#### 2b. Menu System
- [ ] Main menu: Chat, Settings, Tools, Memory, About
- [ ] Settings: WiFi, API provider (Anthropic/OpenAI/OpenRouter), model selection, display brightness, theme
- [ ] Memory viewer: read SOUL.md, USER.md, MEMORY.md from display
- [ ] Keyboard shortcut reference screen

#### 2c. microSD Integration
- [ ] Load SOUL.md / USER.md / MEMORY.md from microSD if present (override NVS)
- [ ] Export conversation logs to microSD as timestamped markdown files
- [ ] Load custom tool definitions from microSD

#### 2d. UX Polish
- [ ] Typing indicator while waiting for LLM response
- [ ] Shake-to-cancel (IMU) during generation
- [ ] Auto-dim display after inactivity, wake on keypress
- [ ] Battery level in status bar (ADC reading)

**Deliverable:** CardClaw is fully self-contained — configure, chat, and manage memory without ever plugging into a computer.

---

### Phase 3 — Security Tools Integration

**Goal:** Register hacking/security capabilities as zclaw tools that the AI agent can invoke via natural language.

#### 3a. WiFi Scanner Tool
- [ ] Register `wifi_scan` as a zclaw tool
- [ ] Scan nearby networks, return SSID, BSSID, channel, signal strength, encryption type
- [ ] Agent can reason about results: "scan my network and flag anything unusual"
- [ ] Display scan results in a formatted table on screen

#### 3b. IR Tools
- [ ] Register `ir_capture` tool: record raw IR signal from environment
- [ ] Register `ir_replay` tool: replay a captured signal
- [ ] Register `ir_identify` tool: attempt to identify protocol (NEC, RC5, Sony SIRC) from timing data
- [ ] Store captured signals on microSD as JSON
- [ ] Agent can chain: "capture the IR signal, identify the protocol, and save it as living-room-tv"

#### 3c. BLE Scanner Tool
- [ ] Register `ble_scan` tool: scan for BLE advertising packets
- [ ] Return device name, MAC, RSSI, service UUIDs, manufacturer data
- [ ] Agent can analyze: "scan for Bluetooth devices and tell me what types they are"

#### 3d. Network Probe Tool
- [ ] Register `net_probe` tool: given an IP, check common ports (22, 80, 443, 8080)
- [ ] Return open/closed status
- [ ] Agent can combine: "scan WiFi, find devices on my network, and check which ones have open web servers"

**Deliverable:** CardClaw can scan WiFi, BLE, and IR — and the AI agent reasons about the results conversationally.

---

### Phase 4 — Audio & Advanced Features

**Goal:** Leverage the Cardputer ADV's upgraded audio hardware for voice and sound features.

#### 4a. Voice Input (Optional Cloud STT)
- [ ] Record audio via ES8311 codec + MEMS mic
- [ ] Send audio to a cloud STT endpoint (Whisper API, Gemini Flash, or Deepgram)
- [ ] Feed transcribed text into the agent loop as if typed
- [ ] Push-to-talk via a keyboard shortcut (e.g., hold Fn+Space)

#### 4b. Audio Feedback
- [ ] Play a short confirmation tone on successful tool execution
- [ ] Optional TTS: send agent response to a cloud TTS endpoint, play via speaker/3.5mm
- [ ] Audio alerts for scheduled task triggers

#### 4c. IMU Gesture Tools
- [ ] Register `imu_read` tool: return current accelerometer/gyroscope values
- [ ] Gesture detection: shake, tilt left/right, flip face-down
- [ ] Map gestures to actions: shake = cancel, flip = DND mode, double-tap = repeat last command
- [ ] Agent can query: "what's my current orientation?" (useful for IoT mount scenarios)

#### 4d. LoRa / Meshtastic Integration (If LoRa Cap Attached)
- [ ] Detect LoRa cap on EXT header at boot
- [ ] Register `lora_send` / `lora_receive` tools
- [ ] Agent can send/receive off-grid messages via natural language
- [ ] GPS position logging if GNSS module present on LoRa cap

**Deliverable:** CardClaw supports voice input, audio feedback, gesture control, and optional LoRa messaging.

---

### Phase 5 — Community & Ecosystem

**Goal:** Make CardClaw a real open-source project with community traction.

#### 5a. Documentation
- [ ] Comprehensive README with hardware photos, feature list, quickstart
- [ ] Architecture document (ARCHITECTURE.md)
- [ ] Contributing guide with code style, PR process, testing expectations
- [ ] Wiki with per-feature deep dives
- [ ] Video demo / walkthrough

#### 5b. Distribution
- [ ] Web flasher (browser-based, no toolchain needed) — follow zclaw's pattern
- [ ] Pre-compiled binaries on GitHub Releases for every tagged version
- [ ] Submit to M5Burner registry (searchable as "CardClaw")
- [ ] Listing on awesome-m5stack-cardputer GitHub repo

#### 5c. Extensibility
- [ ] Custom tool definition format (JSON on microSD) so users can add tools without recompiling
- [ ] Theme system: color schemes loadable from microSD (INI or JSON)
- [ ] Plugin architecture for community-contributed tool modules
- [ ] SOUL.md template gallery (different personas: security analyst, study buddy, ops assistant, etc.)

#### 5d. Community
- [ ] Discord server or channel within existing Cardputer Discord
- [ ] GitHub Discussions for feature requests and show-and-tell
- [ ] First-time contributor issues tagged `good-first-issue`
- [ ] Monthly changelog / dev blog posts (candidate for saminprogress.dev content)

**Deliverable:** CardClaw is installable by non-developers, extensible by the community, and documented well enough for contributors to onboard.

---

## Non-Goals (Explicit Scope Boundaries)

- **On-device LLM inference:** The ESP32-S3 with 320KB RAM cannot run even the smallest LLMs locally. CardClaw is a thin client by design — intelligence lives in the cloud, the device is the interface and sensor platform.
- **Full Flipper Zero replacement:** CardClaw is not trying to be Bruce. Security tools are registered as AI-assistable tools, not standalone offensive utilities.
- **Multi-user / server mode:** CardClaw is a single-user personal device. No user management, no shared state.
- **Android/iOS companion app:** Telegram is the remote channel. No custom mobile app planned.

---

## Technical Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| 320KB RAM too tight for display + agent + WiFi | High | Profile aggressively. Display uses a minimal framebuffer (240×135×2 = ~64KB). Agent loop reuses buffers. WiFi and agent on separate cores. |
| zclaw architecture assumes headless operation | Medium | The inbound message queue is channel-agnostic. Adding a local channel is architecturally clean — no core agent changes needed. |
| ST7789V2 driver doesn't exist for raw ESP-IDF | Medium | Port from TFT_eSPI or M5Unified. Both are well-documented. Alternatively use LVGL with ST7789 driver. |
| TCA8418 has no ESP-IDF library | Low | Simple I2C register protocol. Datasheet is public. ~200 lines of C. |
| API costs for always-on agent | Low | zclaw already supports Anthropic, OpenAI, and OpenRouter. User chooses their provider/model. Haiku-class models keep costs minimal. |
| Firmware size exceeds 4MB partition | Very Low | Current estimate is ~920KB. Even with aggressive feature additions, staying under 2MB is realistic. |

---

## Success Metrics

- **v0.1 release:** Functional chat via keyboard + display on Cardputer ADV
- **50 GitHub stars** within first month of public release
- **Listed on awesome-m5stack-cardputer** and M5Burner registry
- **3+ community-contributed tools** within first quarter
- **Featured on Hackaday, Hackster, or CNX Software** (organic, not pitched)

---

## Naming Candidates

- **CardClaw** — card-sized + *claw ecosystem. Clear, memorable.
- **PocketClaw** — emphasizes portability.
- **TinyClaw** — emphasizes minimalism (but could clash with zclaw's "tiny" positioning).
- **ClawPuter** — playful mashup of Cardputer + *claw.

Current working name: **CardClaw**

---

## License

MIT (matching zclaw upstream).

---

*Roadmap version: 0.1*
*Last updated: March 2026*
*Author: Sam (saminprogress)*
