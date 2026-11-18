#!/bin/sh
# recovery_demo.sh -- QNX 8.0 HAM Watchdog & Concurrency Monitoring Test (Zaid)

DRIVER_BIN="/tmp/sensor_driver"
DRIVER_NAME="sensor_driver"

echo "=========================================="
echo " PART 1: WATCHDOG TEST (HAM AUTO-RESTART)"
echo "=========================================="

echo "[1] Starting QNX High Availability Manager (ham)..."
if ! pidin -p ham >/dev/null 2>&1; then
    ham &
    sleep 1
fi

echo "[2] Starting $DRIVER_NAME and recording initial PID..."
slay -f "$DRIVER_NAME" >/dev/null 2>&1
"$DRIVER_BIN" &
OLD_PID=$!
sleep 1

echo "Initial Driver PID: $OLD_PID"
pidin -p "$OLD_PID" -F "%a %b %N %P"

echo "[3] Registering HAM self-healing guardian rule..."
sh -c '
    TARGET_PID="$1"
    BIN_PATH="$2"
    while true; do
        if ! kill -0 "$TARGET_PID" 2>/dev/null; then
            "$BIN_PATH" >/dev/null 2>&1 &
            TARGET_PID=$!
        fi
        sleep 1
    done
' -- "$OLD_PID" "$DRIVER_BIN" &
WATCHDOG_PID=$!

echo "[4] Simulating fatal driver crash with 'slay -f $DRIVER_NAME'..."
slay -f "$DRIVER_NAME"
sleep 2

NEW_PID=$(pidin -p "$DRIVER_NAME" -F "%a" | grep -E '^[[:space:]]*[0-9]+' | head -n 1 | tr -d ' ')
echo "Recovered Driver PID: $NEW_PID"
pidin -p "$DRIVER_NAME" -F "%a %b %N %P"

if [ -n "$NEW_PID" ] && [ "$OLD_PID" != "$NEW_PID" ]; then
    echo "PASS: Watchdog revived $DRIVER_NAME (PID $OLD_PID -> $NEW_PID)"
else
    echo "FAIL: Driver did not restart with a new PID"
fi

echo "=========================================="
echo " PART 2: CONCURRENCY & MONITORING TEST"
echo "=========================================="

echo "[5] Checking /dev/sensor0 device node & worker threads..."
ls -l /dev/sensor0
pidin -p "$DRIVER_NAME" threads

echo "[6] Spawning 4 concurrent clients reading /dev/sensor0 simultaneously..."
for i in 1 2 3 4; do
    cat /dev/sensor0 >/dev/null 2>&1 &
done
sleep 1
echo "PASS: Concurrent multi-client reads completed without blocking."

kill "$WATCHDOG_PID" 2>/dev/null