#!/bin/bash
/*=============================================================================
 *  benchmark_kev.sh - Automated .kev Kernel Trace & Stress Benchmark
 *  QNX SDP 8.0 - THE QNX QUIRKS - Team: Member 7 (Lidiya)
 *===========================================================================*/
set -euo pipefail

# --- Bash-native cycles-to-ns converter (replaces C inline from perf_trace.h) ---
# Formula: ns = (delta_cycles * 1_000_000_000) / cycles_per_sec
# Default cycles_per_sec for QNX SDP 8.0; override via env var if known.
cycles_to_ns() {
    local delta_cycles=$1
    local cpu_sec=${QNX_CYCLES_PER_SEC:-1000000000}  # 1 GHz default = 1 ns/cycle
    if [ "$cpu_sec" -eq 0 ]; then
        echo "0"
        return
    fi
    # Use bc for integer arithmetic; fallback to shell arithmetic
    local result=$(echo "scale=0; ($delta_cycles * 1000000000) / $cpu_sec" 2>/dev/null)
    echo "${result:-0}"
}

# --- Configuration ----------------------------------------------------------
KEV_IDLE="/tmp/qnx_quirks_idle.kev"
KEV_LOAD="/tmp/qnx_quirks_load.kev"
STRESS_DURATION=10          # seconds of synthetic load
BASELINE_DURATION=5         # seconds of idle baseline
TRACELGGER_OPTS="-n 100000 -o 1000 -p 4 -S -C"  # ring buffer, 100MB max, 4 procs, daemon mode
STRESS_PID_FILE="/tmp/qnx_quirks_stress.pid"
TRACE_PID_FILE="/tmp/qnx_quirks_tracer.pid"

# Colour helpers for terminal output (QNX terminal assumes 256-colour support)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Colour

print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# --- Step 1: Verify /dev/sensor0 (or dummy_driver) is running -------------
print_status "Step 1: Verifying driver is running..."

if [ ! -c /dev/sensor0 ]; then
    if [ -c /dev/dummy_driver ]; then
        print_warning "/dev/sensor0 not found, using /dev/dummy_driver"
        SENSOR_DEV="/dev/dummy_driver"
    else
        print_error "Neither /dev/sensor0 nor /dev/dummy_driver found!"
        print_error "Start the driver first: /bin/sensor_driver (or /bin/dummy_driver)"
        exit 1
    fi
else
    SENSOR_DEV="/dev/sensor0"
fi

# Get the PID of the driver - try common names
DRIVER_PID=$(ps -o pid,comm -c | grep -E 'sensor_driver|dummy_driver|security_init' | head -1 | awk '{print $1}')

if [ -z "$DRIVER_PID" ]; then
    # Fallback: search by device node major/minor or just list all processes
    DRIVER_PID=$(ps -eo pid,comm | grep -E 'sensor_driver|dummy_driver' | head -1 | awk '{print $1}')
fi

if [ -z "$DRIVER_PID" ]; then
    print_error "Could not detect driver PID! Is the driver loaded?"
    exit 1
fi

print_status "Driver detected at PID: $DRIVER_PID"

# Print non-root UID and locked abilities via pidin
print_status "Step 2: Checking driver UID and locked abilities via pidin..."
pidin -p "$DRIVER_PID" user
pidin -p "$DRIVER_PID" info | grep -E 'ability|procmgr' || true

# --- Step 2: Launch tracelogger for idle baseline ---------------------------
print_status "Step 3: Starting tracelogger for ${BASELINE_DURATION}s idle baseline..."

# Clean up any previous tracelogger residues
if [ -f "$KEV_IDLE" ]; then
    rm -f "$KEV_IDLE"
fi
if [ -f "$KEV_LOAD" ]; then
    rm -f "$KEV_LOAD"
fi

# Start tracelogger in ring/daemon mode capturing:
# - Kernel scheduling/interrupt events
# - User TraceEvent strings (our custom subclasses)
tracelogger $TRACELGGER_OPTS -b "$KEV_IDLE" &
TRACELGGER_PID=$!
echo "$TRACELGGER_PID" > "$TRACE_PID_FILE"
print_status "tracelogger started PID=$TRACELGGER_PID writing to $KEV_IDLE"

# Wait for the baseline period
sleep "$BASELINE_DURATION"

# --- Step 3: Launch synthetic CPU load (background busy loops) ------------
print_status "Step 4: Starting synthetic CPU load for ${STRESS_DURATION}s..."

# Spawn N background busy loops (one per CPU core, minus 1 for safety)
# Using 'idle' loop that just spins on gettimeofday-free work
CORES=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo 1)
if [ "$CORES" -gt 1 ]; then
    CORES=$((CORES - 1))
else
    CORES=1
fi

print_status "Spawning $CORES background stress workers..."

for i in $(seq 1 "$CORES"); do
    (
        # Spin-lock: just iterate a lot - no kernel calls, pure user CPU
        local_iter=0
        while [ "$local_iter" -lt 500000 ]; do
            : $((local_iter += 1))
        done
    ) &
    echo $! >> "$STRESS_PID_FILE"
done

# Wait for the stress period
sleep "$STRESS_DURATION"

# --- Step 4: Cleanly terminate all stress workers and tracelogger ----------
print_status "Step 5: Terminating stress workers and tracelogger..."

# Stop accepting new events on the tracelogger buffer
kill "$TRACELGGER_PID" 2>/dev/null || true
wait "$TRACELGGER_PID" 2>/dev/null || true

# Kill all stress worker background processes
if [ -f "$STRESS_PID_FILE" ]; then
    while IFS= read -r pid; do
        kill "$pid" 2>/dev/null || true
    done < "$STRESS_PID_FILE"
    rm -f "$STRESS_PID_FILE"
fi

# Give a moment for buffers to flush
sleep 1

# If tracelogger is still running, force-terminate
if kill -0 "$TRACELGGER_PID" 2>/dev/null; then
    kill -9 "$TRACELGGER_PID" 2>/dev/null || true
fi

print_status "All workers terminated."

# --- Step 5: Run traceprinter with filtering ------------------------------
print_status "Step 6: Running traceprinter with filtering..."

# Print idle KEV contents with our custom events filtered
print_status "--- Idle trace events (custom user events) ---"
traceprinter -b "$KEV_IDLE" -S _NTO_TRACE_USRSTREVENT_CLASS 2>/dev/null || \
    traceprinter -b "$KEV_IDLE" | grep -E 'USRSTREVENT|usrstrevent' || \
    print_status "No custom user events found in idle trace (expected if no tracing enabled)."

print_status "--- Load trace events (custom user events) ---"
traceprinter -b "$KEV_LOAD" -S _NTO_TRACE_USRSTREVENT_CLASS 2>/dev/null || \
    traceprinter -b "$KEV_LOAD" | grep -E 'USRSTREVENT|usrstrevent' || \
    print_status "No custom user events found in load trace."

# --- Step 6: Calculate IRQ-to-client latency deltas -----------------------
print_status "Step 7: Calculating IRQ-to-client latency deltas..."

# Extract events in chronological order for both baseline and load
# We look for: IRQ pulse events (subclass 0x01) and client delivery events (subclass 0x03)
# and compute the cycle delta between IRQ pulse and client delivery.

echo ""
echo "============================================================"
echo "  QNX QUIRKS KEV Benchmark Results"
echo "============================================================"
echo ""

# Function to extract cycle timestamps from KEV file for a given subclass
extract_cycles() {
    local kev_file="$1"
    local subclass="$2"
    traceprinter -b "$kev_file" -S _NTO_TRACE_USRSTREVENT_CLASS 2>/dev/null | \
        grep -i "subclass.*${subclass}" | \
        grep -oP '(?<=arg1 = )\d+' || echo ""
}

echo "--- Idle baseline IRQ pulse cycles ---"
IDLE_IRQ_CYCLES=$(extract_cycles "$KEV_IDLE" 1)
echo "$IDLE_IRQ_CYCLES"

echo "--- Load IRQ pulse cycles ---"
LOAD_IRQ_CYCLES=$(extract_cycles "$KEV_LOAD" 1)
echo "$LOAD_IRQ_CYCLES"

echo "--- Idle client delivered cycles ---"
IDLE_CLIENT_CYCLES=$(extract_cycles "$KEV_IDLE" 3)
echo "$IDLE_CLIENT_CYCLES"

echo "--- Load client delivered cycles ---"
LOAD_CLIENT_CYCLES=$(extract_cycles "$KEV_LOAD" 3)
echo "$LOAD_CLIENT_CYCLES"

# Calculate deltas if we have data
if [ -n "$IDLE_IRQ_CYCLES" ] && [ -n "$IDLE_CLIENT_CYCLES" ]; then
    IDLE_DELTA=$((IDLE_CLIENT_CYCLES - IDLE_IRQ_CYCLES))
    IDLE_NS=$(cycles_to_ns "$IDLE_DELTA" 2>/dev/null || echo "N/A")
    echo "Idle IRQ-to-client delta: ${IDLE_DELTA} cycles (${IDLE_NS} ns)"
fi

if [ -n "$LOAD_IRQ_CYCLES" ] && [ -n "$LOAD_CLIENT_CYCLES" ]; then
    LOAD_DELTA=$((LOAD_CLIENT_CYCLES - LOAD_IRQ_CYCLES))
    LOAD_NS=$(cycles_to_ns "$LOAD_DELTA" 2>/dev/null || echo "N/A")
    echo "Under-load IRQ-to-client delta: ${LOAD_DELTA} cycles (${LOAD_NS} ns)"
fi

echo ""
echo "============================================================"
echo "  Kev benchmark complete."
echo "  Idle trace:  $KEV_IDLE"
echo "  Load trace:  $KEV_LOAD"
echo "============================================================"

exit 0