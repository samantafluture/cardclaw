#!/bin/bash
# Run zclaw in QEMU emulator

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
QEMU_BUILD_DIR="build-qemu"
QEMU_SDKCONFIG="sdkconfig.qemu"
QEMU_PID_FILE="$QEMU_BUILD_DIR/qemu.pid"
QEMU_SDKCONFIG_DEFAULTS_BASE="sdkconfig.defaults;sdkconfig.qemu.defaults"
LIVE_API_MODE=0
LIVE_API_PROVIDER="auto"
LIVE_API_LOGS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --live-api)
            LIVE_API_MODE=1
            shift
            ;;
        --live-api-provider)
            if [ $# -lt 2 ]; then
                echo "Error: --live-api-provider requires a value: auto|anthropic|openai"
                exit 1
            fi
            LIVE_API_PROVIDER="$2"
            shift 2
            ;;
        --live-api-logs)
            LIVE_API_LOGS=1
            shift
            ;;
        *)
            echo "Usage: $0 [--live-api] [--live-api-provider auto|anthropic|openai] [--live-api-logs]"
            exit 1
            ;;
    esac
done

if [[ "$LIVE_API_PROVIDER" != "auto" && "$LIVE_API_PROVIDER" != "anthropic" && "$LIVE_API_PROVIDER" != "openai" ]]; then
    echo "Error: invalid --live-api-provider '$LIVE_API_PROVIDER' (expected auto|anthropic|openai)"
    exit 1
fi

cd "$PROJECT_DIR"

# Check for QEMU
if ! command -v qemu-system-riscv32 &> /dev/null; then
    echo "QEMU not found. Install it:"
    echo "  macOS:  brew install qemu"
    echo "  Ubuntu: apt install qemu-system-misc"
    exit 1
fi

# Find and source ESP-IDF
if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    source "$HOME/esp/esp-idf/export.sh"
elif [ -f "$HOME/esp/v5.4/esp-idf/export.sh" ]; then
    source "$HOME/esp/v5.4/esp-idf/export.sh"
elif [ -n "$IDF_PATH" ]; then
    source "$IDF_PATH/export.sh"
else
    echo "Error: ESP-IDF not found"
    exit 1
fi

# Build emulator profile
echo "Building QEMU profile..."
# Always regenerate sdkconfig.qemu from defaults to avoid stale local overrides.
rm -f "$QEMU_SDKCONFIG"

QEMU_SDKCONFIG_DEFAULTS="$QEMU_SDKCONFIG_DEFAULTS_BASE"
if [ "$LIVE_API_MODE" -eq 1 ]; then
    case "$LIVE_API_PROVIDER" in
        anthropic)
            if [ -z "${ANTHROPIC_API_KEY:-}" ]; then
                echo "Error: ANTHROPIC_API_KEY is required for --live-api-provider anthropic"
                exit 1
            fi
            ;;
        openai)
            if [ -z "${OPENAI_API_KEY:-}" ]; then
                echo "Error: OPENAI_API_KEY is required for --live-api-provider openai"
                exit 1
            fi
            ;;
        auto)
            if [ -z "${ANTHROPIC_API_KEY:-}" ] && [ -z "${OPENAI_API_KEY:-}" ]; then
                echo "Error: set ANTHROPIC_API_KEY or OPENAI_API_KEY for --live-api mode"
                exit 1
            fi
            ;;
    esac

    mkdir -p "$QEMU_BUILD_DIR"
    LIVE_DEFAULTS="$QEMU_BUILD_DIR/sdkconfig.qemu.live.defaults"
    cat > "$LIVE_DEFAULTS" <<'EOF'
CONFIG_ZCLAW_STUB_LLM=n
CONFIG_ZCLAW_EMULATOR_LIVE_LLM=y
CONFIG_ZCLAW_STUB_TELEGRAM=y
CONFIG_ZCLAW_CHANNEL_UART=y
CONFIG_ZCLAW_EMULATOR_MODE=y
CONFIG_ZCLAW_GPIO_MIN_PIN=2
CONFIG_ZCLAW_GPIO_MAX_PIN=10
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
EOF
    QEMU_SDKCONFIG_DEFAULTS="sdkconfig.defaults;$LIVE_DEFAULTS"
fi

idf.py \
    -B "$QEMU_BUILD_DIR" \
    -D SDKCONFIG="$QEMU_SDKCONFIG" \
    -D SDKCONFIG_DEFAULTS="$QEMU_SDKCONFIG_DEFAULTS" \
    build

echo "Starting QEMU emulator..."
if [ "$LIVE_API_MODE" -eq 1 ]; then
    echo "Live API mode enabled: requests are proxied from host -> API provider."
    echo "Provider selection: $LIVE_API_PROVIDER"
    if [ "$LIVE_API_PROVIDER" = "anthropic" ]; then
        echo "Using ANTHROPIC_API_KEY from host environment."
    elif [ "$LIVE_API_PROVIDER" = "openai" ]; then
        echo "Using OPENAI_API_KEY from host environment."
    else
        echo "Auto mode: bridge infers provider from request format (Anthropic/OpenAI)."
    fi
else
    echo "Note: WiFi/TLS don't work in QEMU. Enable stub mode via menuconfig for testing."
fi
echo "Press Ctrl+A, X to exit."
echo ""

# Merge bootloader + partition table + app into single image
esptool.py --chip esp32c3 merge_bin -o "$QEMU_BUILD_DIR/merged.bin" \
    --flash_mode dio --flash_size 4MB \
    0x0 "$QEMU_BUILD_DIR/bootloader/bootloader.bin" \
    0x8000 "$QEMU_BUILD_DIR/partition_table/partition-table.bin" \
    0x20000 "$QEMU_BUILD_DIR/zclaw.bin" 2>/dev/null

# QEMU for ESP32 requires fixed-size flash images (2/4/8/16MB).
# Pad merged image to 4MB so it is always accepted.
QEMU_FLASH_IMAGE="$QEMU_BUILD_DIR/merged-qemu-4mb.bin"
cp "$QEMU_BUILD_DIR/merged.bin" "$QEMU_FLASH_IMAGE"
truncate -s 4M "$QEMU_FLASH_IMAGE"

# Run QEMU
rm -f "$QEMU_PID_FILE"
trap 'rm -f "$QEMU_PID_FILE"' EXIT
QEMU_CMD=(
    qemu-system-riscv32
    -M esp32c3
    -drive "file=$QEMU_FLASH_IMAGE,if=mtd,format=raw"
    -pidfile "$QEMU_PID_FILE"
    -serial mon:stdio
    -nographic
)

if [ "$LIVE_API_MODE" -eq 1 ]; then
    BRIDGE_ARGS=(--provider "$LIVE_API_PROVIDER")
    if [ "$LIVE_API_LOGS" -eq 1 ]; then
        BRIDGE_ARGS+=(--bridge-logs)
    fi
    python3 "$SCRIPT_DIR/qemu_live_llm_bridge.py" "${BRIDGE_ARGS[@]}" -- "${QEMU_CMD[@]}"
else
    "${QEMU_CMD[@]}"
fi
