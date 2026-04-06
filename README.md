# CardClaw

Open-source AI agent firmware for the **M5Stack Cardputer ADV** (ESP32-S3). Fork of [zclaw](https://github.com/tnm/zclaw) with native keyboard/display UI and integrated security tools.

## What is this?

zclaw is the smallest possible AI personal assistant for ESP32 — chat via Telegram, schedule tasks, control GPIO, all in ~888KB of firmware. CardClaw extends it with:

- **Native display** — ST7789V2 SPI terminal renderer with status bar
- **Native keyboard** — TCA8418 I2C, 56 keys with Fn/Ctrl/Alt modifiers
- **Local channel** — keyboard-to-agent-to-display, alongside Telegram and serial
- **Security tools** — WiFi scanner, BLE scanner, IR capture/replay, net probe
- **HAL abstraction** — clean hardware interface for all Cardputer ADV peripherals

## Target Hardware

**M5Stack Cardputer ADV** — ESP32-S3FN8, 8MB flash, 320KB SRAM, no PSRAM.

## Quick Start

> Prerequisites: ESP-IDF v5.5+ installed. See [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/).

```bash
git clone https://github.com/samantafluture/cardclaw.git
cd cardclaw
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Upstream

CardClaw tracks [tnm/zclaw](https://github.com/tnm/zclaw) as upstream. Core agent loop, tool registry, memory system, Telegram client, and serial CLI are inherited from zclaw.

```bash
git remote add upstream https://github.com/tnm/zclaw.git
git fetch upstream
```

## Docs

- `docs/cardclaw-prd.md` — Product requirements
- `docs/cardclaw-tdd.md` — Technical design
- `docs/cardclaw-roadmap.md` — Phase-by-phase roadmap

## License

MIT
