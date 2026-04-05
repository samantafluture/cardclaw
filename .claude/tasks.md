# Project: CardClaw

> Last agent update: 2026-04-04

## Active Sprint

### P0 -- Must do now

- [ ] Fork zclaw repository and set up ESP-IDF v5.5+ dev environment (WSL2) `[M]` #setup
- [ ] Create Cardputer ADV board config — sdkconfig.defaults + partition table for 8MB flash `[M]` #setup
- [ ] Verify zclaw core compiles for ESP32-S3 target `[S]` #setup
- [ ] Flash and confirm headless mode works (WiFi, Telegram, serial CLI) `[M]` #setup #testing
- [ ] Implement ST7789V2 SPI display driver `[L]` #backend #hal
  - [ ] Minimal terminal renderer (fixed-width font, char grid, auto-scroll)
  - [ ] Status bar (WiFi, battery %, model indicator)
  - [ ] Input bar with cursor
- [ ] Implement TCA8418 I2C keyboard driver `[M]` #backend #hal
  - [ ] 56-key mapping including Fn, Ctrl, Alt, Opt modifiers
  - [ ] Key repeat, debounce
  - [ ] Line buffer with basic editing (backspace, Ctrl+U clear)
- [ ] Create local channel (keyboard → agent loop → display) `[M]` #backend
  - [ ] Pipe keyboard input into zclaw inbound message queue
  - [ ] Route agent responses to display
  - [ ] All three channels (local, Telegram, serial) active simultaneously

### P1 -- Should do this week

- [ ] Build HAL abstraction layer (hal.h interface) `[M]` #backend #hal
  - [ ] Display, keyboard, IMU, IR, audio, storage, power interfaces
- [ ] Token-by-token streaming display as LLM responds `[M]` #backend
- [ ] Scroll back through conversation history (Fn+Up/Down) `[S]` #backend
- [ ] Typing/thinking indicator while waiting for LLM response `[S]` #frontend
- [ ] Set up GitHub repo with MIT license, CONTRIBUTING.md, issue templates `[S]` #devops
- [ ] Document build process in README `[S]` #content

### P2 -- Nice to have

- [ ] WiFi scan tool — register as zclaw tool, formatted table display `[M]` #backend #security
- [ ] BLE scan tool — scan advertising packets, return device info `[M]` #backend #security
- [ ] On-device WiFi setup wizard (scan → select → type password) `[M]` #frontend
- [ ] On-device API key entry (or load from microSD config) `[S]` #frontend
- [ ] Menu system (Chat, Settings, Tools, Memory, About) `[M]` #frontend
- [ ] microSD integration — load SOUL.md/USER.md/MEMORY.md, export logs `[M]` #backend
- [ ] IR capture/replay/identify tools `[L]` #backend #security
- [ ] Net probe tool (port scanner) `[S]` #backend #security
- [ ] IMU driver (BMI270) + shake-to-cancel gesture `[M]` #backend #hal
- [ ] Battery level reading via ADC in status bar `[S]` #backend #hal
- [ ] Auto-dim display after inactivity `[S]` #frontend
- [ ] Theme/color scheme loadable from microSD `[S]` #frontend
- [ ] Custom tool definitions from microSD (JSON format) `[M]` #backend
- [ ] Audio feedback — confirmation tones on tool execution `[S]` #backend
- [ ] Web flasher for browser-based installation `[L]` #devops #launch

## Blocked

## Completed (recent)

## Notes
- Fork of zclaw (not MimiClaw) — fits 8MB flash / no PSRAM constraints
- Target: M5Stack Cardputer ADV (ESP32-S3FN8, 8MB flash, 320KB SRAM, no PSRAM)
- Firmware budget: ~920KB estimated, ~3MB+ headroom
- zclaw baseline: ~833KB (agent + ESP-IDF + WiFi + TLS)
- C on ESP-IDF (not Arduino), FreeRTOS dual-core (Core 0 = I/O, Core 1 = Agent)
- Check docs/cardclaw-prd.md, docs/cardclaw-tdd.md, docs/cardclaw-roadmap.md for full specs
