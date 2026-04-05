# CardClaw

Open-source AI agent firmware for the M5Stack Cardputer ADV (ESP32-S3). Fork of zclaw with native keyboard/display UI and integrated security tools.

## Quick Context

- **Language:** C (C11), ESP-IDF v5.5+ / FreeRTOS
- **Target:** M5Stack Cardputer ADV — ESP32-S3FN8, 8MB flash, 320KB SRAM, no PSRAM
- **Architecture:** Thin client — AI reasoning in the cloud, device is sensor platform + UI
- **Upstream:** Fork of zclaw (MIT)

## Project Structure

See `docs/cardclaw-tdd.md` §5.2 for the full planned directory layout. Key areas:
- `main/hal/` — Hardware abstraction layer (display, keyboard, IMU, IR, audio, power)
- `main/ui/` — Terminal renderer, status bar, menu, input handling
- `main/agent/` — ReAct agent loop, context builder, tool registry (zclaw-derived)
- `main/channels/` — Local (keyboard/display), Telegram, serial CLI
- `main/network/` — WiFi manager, LLM client, HTTP/Telegram clients
- `main/tools/` — Tool implementations (WiFi scan, BLE scan, IR, GPIO, etc.)
- `main/storage/` — NVS config, LittleFS, microSD

## Key Constraints

- **320KB RAM budget** — no full framebuffer, stream everything, free buffers immediately
- **~2.5MB firmware partition** — keep binary lean (~1MB target)
- **Dual-core split:** Core 0 = I/O tasks, Core 1 = agent loop exclusively
- **All hardware access through HAL** — no direct GPIO/SPI/I2C outside `hal/`

## Docs

- `docs/cardclaw-prd.md` — Product requirements, user stories, milestones
- `docs/cardclaw-tdd.md` — Technical design, HAL interface, module specs, memory budget
- `docs/cardclaw-roadmap.md` — Phase-by-phase roadmap, firmware size budget, risks

## Task Management

- Tasks are tracked in `.claude/tasks.md` -- this is the single source of truth
- Before starting work, read `.claude/tasks.md` to understand priorities
- Respect priority order: finish all P0 before starting P1
- **CRITICAL: After ANY change to tasks.md, immediately commit and push**
  - Commit message: `chore: update tasks` (always this exact message)
  - Never batch task updates with code changes in the same commit
  - Push immediately after commit -- the VPS webhook syncs within seconds

### Private context
- Private project notes are in `.claude/private/` (secrets, infra, strategy)
- Read `.claude/private/` for context on credentials, infrastructure, strategic decisions
- NEVER include contents of `.claude/private/` in commit messages, PRs, or public output
