#!/bin/bash
# Release a busy serial port by stopping holder processes.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

PORT=""
DRY_RUN=false
HARD_KILL=false

usage() {
    echo "Usage: $0 [PORT] [--hard] [--dry-run]"
    echo ""
    echo "Stops all processes currently holding the selected serial port."
    echo ""
    echo "Options:"
    echo "  --hard     Follow TERM with KILL (-9) for surviving targeted holders"
    echo "  --dry-run  Show what would be killed without sending signals"
}

detect_serial_ports() {
    local ports=()
    local os_name

    os_name="$(uname -s)"
    shopt -s nullglob
    if [ "$os_name" = "Darwin" ]; then
        ports+=(/dev/cu.usbserial-*)
        ports+=(/dev/cu.usbmodem*)
        if [ "${#ports[@]}" -eq 0 ]; then
            ports+=(/dev/tty.usbserial-*)
            ports+=(/dev/tty.usbmodem*)
        fi
    else
        ports+=(/dev/ttyUSB*)
        ports+=(/dev/ttyACM*)
    fi
    shopt -u nullglob

    local p
    for p in "${ports[@]}"; do
        [ -e "$p" ] && echo "$p"
    done
}

normalize_serial_port() {
    local port="$1"
    local callout_port

    case "$port" in
        /dev/tty.usb*)
            callout_port="/dev/cu.${port#/dev/tty.}"
            if [ -e "$callout_port" ]; then
                echo "$callout_port"
                return
            fi
            ;;
    esac

    echo "$port"
}

select_serial_port() {
    local candidates=()
    local p

    while IFS= read -r p; do
        [ -n "$p" ] && candidates+=("$p")
    done < <(detect_serial_ports)

    if [ "${#candidates[@]}" -eq 0 ]; then
        return 1
    fi

    if [ "${#candidates[@]}" -eq 1 ]; then
        PORT="${candidates[0]}"
        echo "Auto-detected serial port: $PORT"
        return 0
    fi

    echo "Multiple serial ports detected:"
    local i
    for ((i = 0; i < ${#candidates[@]}; i++)); do
        echo "  $((i + 1)). ${candidates[$i]}"
    done

    if [ -t 0 ]; then
        read -r -p "Select device [1-${#candidates[@]}]: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#candidates[@]}" ]; then
            PORT="${candidates[$((choice - 1))]}"
            echo "Using selected serial port: $PORT"
            return 0
        fi
        echo "Invalid selection."
        return 1
    fi

    PORT="${candidates[0]}"
    echo "Non-interactive shell; defaulting to first detected port: $PORT"
    return 0
}

port_holder_pids() {
    local port="$1"

    if command -v lsof >/dev/null 2>&1; then
        lsof -t "$port" 2>/dev/null | sort -u || true
        return
    fi

    if command -v fuser >/dev/null 2>&1; then
        fuser "$port" 2>/dev/null | tr ' ' '\n' | sed -E 's/[^0-9].*$//' | sed '/^$/d' | sort -u || true
    fi
}

port_is_busy() {
    local port="$1"

    if command -v lsof >/dev/null 2>&1; then
        lsof "$port" >/dev/null 2>&1
        return $?
    fi

    if command -v fuser >/dev/null 2>&1; then
        fuser "$port" >/dev/null 2>&1
        return $?
    fi

    return 1
}

show_port_holders() {
    local port="$1"

    if command -v lsof >/dev/null 2>&1; then
        lsof "$port" 2>/dev/null || true
        return
    fi

    if command -v fuser >/dev/null 2>&1; then
        fuser "$port" 2>/dev/null || true
    fi
}

contains_pid() {
    local pid="$1"
    local lines="$2"
    if [ -z "$lines" ]; then
        return 1
    fi
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if [ "$line" = "$pid" ]; then
            return 0
        fi
    done << EOF
$lines
EOF
    return 1
}

for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=true
            ;;
        --hard)
            HARD_KILL=true
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            echo "Error: unknown option '$arg'"
            usage
            exit 1
            ;;
        *)
            if [ -z "$PORT" ]; then
                PORT="$arg"
            else
                echo "Error: multiple ports provided: '$PORT' and '$arg'"
                usage
                exit 1
            fi
            ;;
    esac
done

if [ -z "$PORT" ]; then
    select_serial_port || true
fi

if [ -z "$PORT" ]; then
    echo "No serial port detected. Usage: $0 [PORT]"
    exit 1
fi

NORMALIZED_PORT="$(normalize_serial_port "$PORT")"
if [ "$NORMALIZED_PORT" != "$PORT" ]; then
    echo "Using callout serial port: $NORMALIZED_PORT"
    PORT="$NORMALIZED_PORT"
fi

if ! port_is_busy "$PORT"; then
    echo "Serial port is already free: $PORT"
    exit 0
fi

echo "Port holders on $PORT:"
show_port_holders "$PORT"
echo ""

pids="$(port_holder_pids "$PORT")"
targets=""
target_count=0

while IFS= read -r pid; do
    [ -n "$pid" ] || continue
    echo "Targeting PID $pid"
    targets="${targets} ${pid}"
    target_count=$((target_count + 1))
done << EOF
$pids
EOF

if [ "$target_count" -eq 0 ]; then
    echo "No holder processes found for $PORT."
    exit 1
fi

if [ "$DRY_RUN" = true ]; then
    echo ""
    echo "Dry run only; no signals sent."
    exit 0
fi

echo ""
for pid in $targets; do
    echo "Sending TERM to PID $pid"
    kill "$pid" 2>/dev/null || true
done

sleep 1
remaining="$(port_holder_pids "$PORT")"

if [ "$HARD_KILL" = true ] && [ -n "$remaining" ]; then
    for pid in $targets; do
        if contains_pid "$pid" "$remaining"; then
            echo "Sending KILL to PID $pid"
            kill -9 "$pid" 2>/dev/null || true
        fi
    done
    sleep 1
fi

if port_is_busy "$PORT"; then
    echo ""
    echo "Serial port is still busy: $PORT"
    show_port_holders "$PORT"
    exit 1
fi

echo ""
echo "Released serial port: $PORT"
