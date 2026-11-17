#=============================================================================
 #  CODELAB.md - Devpost Codelab & 3-Minute Live Demo Storyboard
 #  QNX SDP 8.0 - THE QNX QUIRKS - Team: Member 7 (Lidiya)
 #  Target Awards: "Best Codelab / Tutorial" + "Best Use of QNX-Specific Tech"
 #===========================================================================/

"""
QNX QUIRKS Devpost Codelab - Table of Contents

  1. Target Image Bring-Up
  2. BSP/MMIO (User-Space MMIO & DMA)
  3. User-Space Interrupts via Pulses
  4. POSIX Resource Manager /dev/sensor0
  5. Thread Pool & ham Self-Healing Recovery
  6. procmgr_ability Security & Root-Dropping
  7. tracelogger Kernel Trace & Stress Profiling

Each section includes hands-on exercises, terminal commands,
explanation of QNX-exclusive primitives, and talking points for the
3-minute live demo video.
"""

#=============================================================================
# SECTION 1: Target Image Bring-Up
#=============================================================================

## 1. Target Image Bring-Up

### Objective
Boot a minimal QNX 8.0.5 QEMU x86_64 image (and Raspberry Pi 4 aarch64le)
via `mkqnximage` (`ifs.bin`) with SSH enabled, proving the device is accessible
before any driver code is written.

### Key Commands & Conmands

| Step | Command | Talking Points |
|------|---------|----------------|
| 1.1  | `mkqnximage -o ifs.bin \` <br> `  -l slog2 \` <br> `  -l cam,qtime,devu \` <br>`  -b build \` <br>`  -k kernel \` <br>`  snippets/*.custom` | Explain that `mkqnximage` constructs the IFS (Initial Functioning System)
from listed libraries. The `-l` flags add required QNX kernelservices (slog2 for logging, cam for
resource management, devu for device enumeration). |
| 1.2  | `ssh qnx-target.local "echo 'SSH enabled'"` | Verify SSH is active in the image. The `snippets/*.custom` copy
step (handled by `auto_qnx.py`) inserts the `post_start.custom` script thatstarts `slog2c`
and opens an SSH daemon at boot. |
| 1.3  | `procnto -m 256M -c 1G` | Boot the QNIO microkernel. The `-m` sets memory, `-c` sets CPU
partitioning. In QNX 8.0, the microkernel runs in a separate address spacefrom the
servers, proving the architecture's safety isolation. |
| 1.4  | `ps -eo pid,ppid,user,comm | head -10` | Show that PID 1 is `procnto`, not Linux's init. This is the microkerneldifference:
all drivers are separate processes, not kernel modules. |

### Hands-On Exercise
1. Run `auto_qnx.py compile --src src/driver/dummy_driver.c --out bin/dummy_driver --arch gcc_ntox86_64`
2. Run `auto_qnx.py deploy --mode daemon`
3. Verify the binary appears: `ls -l /bin/dummy_driver`
4. Confirm it runs as non-root: `./bin/dummy_driver &; ps -o pid,user,comm -p $(pgrep dummy_driver)`

---

#=============================================================================
# SECTION 2: BSP/MMIO - User-Space MMIO & DMA
#=============================================================================

## 2. BSP/MMIO - User-Space MMIO & DMA (Basith & Yashwanth)

### Objective
Map physical silicon registers into user space via `mmap_device_memory()`
with `PROT_NOCACHE`, and read/write via `in32()`/`out32()` after requesting
I/O privilege via `ThreadCtl(_NTO_TCTL_IO, 0)`. Support both QEMU x86_64
and Raspberry Pi 4 BCM2711 aarch64le.

### QNX-Exclusive Primitives

| Primitive | Purpose | Code Example |
|-----------|---------|--------------|
| `mmap_device_memory()` | Map physical device registers into the process's address space | ```c<br>void *map = mmap_device_memory(NULL, SIZE, PHYS_ADDR, 0, _NTO_TCARDMAP);``` |
| `PROT_NOCACHE` | Ensure MMIO writes bypass CPU cache (critical for hardware registers) | ```c<br>mmap_device_memory(map, SIZE, PHYS_ADDR, PROT_NOCACHE);``` |
| `ThreadCtl(_NTO_TCTL_IO, 0)` | Request I/O privilege from the microkernel; without this, `in32()/out32()` fault | ```c<br>ThreadCtl(_NTO_TCTL_IO, 0); ``` |
| `#ifdef __aarch64__` / `#ifdef __x86_64__` | Conditional compilation for Pi 4 BCM2711 vs QEMU virtual device | ```c<br>#ifdef __aarch64__<br>  // BCM2711 register offsets<br>#elif defined(__x86_64__)<br>  // QEMU virtual device offsets<br>#endif``` |

### Hands-On Exercise
1. Enable I/O privileges: `ThreadCtl(_NTO_TCTL_IO, 0)` (run as root first)
2. Map a test register: `mmap_device_memory(NULL, 4096, 0x40000000, PROT_NOCACHE)`
3. Read/write: `in32( mapped_base + REG_OFFSET, &val )`
4. Toggle `#ifdef __aarch64__` and rebuild for Pi 4 cross-compile

---

#=============================================================================
# SECTION 3: User-Space Interrupts via Pulses
#=============================================================================

## 3. User-Space Interrupts via Pulses (Aeknath)

### Objective
Replace a kernel ISR with `InterruptAttachEvent()` using `SIGEV_PULSE` to
wake a high-priority driver thread in microseconds. Use `InterruptMask()/InterruptUnmask()`
to control the hardware interrupt line.

### QNX-Exclusive Primitives

| Primitive | Purpose | Code Example |
|-----------|---------|--------------|
| `InterruptAttachEvent()` | Attach a pulse event to a hardware interrupt vector (no kernel ISR needed) | ```c<br>InterruptAttachEvent(veid, event, _PULSE_CODE_UNBLOCK);``` |
| `SIGEV_PULSE` | Signal event type: generate a pulse that unblocks a blocked-receive | ```c<br>struct sigevent ev;<br>SIGEV_PULSE(&ev, chid, 10, _PULSE_CODE_UNBLOCK, 0);``` |
| `InterruptMask()` | Mask (disable) the hardware interrupt line temporarily | ```c<br>InterruptMask(veid, 1);``` |
| `InterruptUnmask()` | Unmask (re-enable) the hardware interrupt line | ```c<br>InterruptUnmask(veid, 0);``` |
| `InterruptWait()` | Block the thread until a pulse is received (microsecond latency) | ```c<br>InterruptWait(veid, NULL);``` |

### Hands-On Exercise
1. Create a pulse channel: `chid = ChannelCreate(0)`
2. Attach interrupt to event: `InterruptAttachEvent(veid, chid, _PULSE_CODE_UNBLOCK)`
3. Mask/unmask in a test: `InterruptMask(veid, 1); InterruptUnmask(veid, 0);`
4. Observe the thread wakes up in < 10 µs via `slog2c` timing

---

#=============================================================================
# SECTION 4: POSIX Resource Manager /dev/sensor0
#=============================================================================

## 4. POSIX Resource Manager /dev/sensor0 (Siddhant)

### Objective
Expose `open()`, `read()`, `write()`, and custom `devctl()` commands
(`__DIOF`/`__DIOT`/`__DIOTF`) via `dispatch_create()` and `resmgr_attach()`.
This is the user-space device interface that clients (phone apps, host tools)
read for telemetry data.

### QNX-Exclusive Primitives

| Primitive | Purpose | Code Example |
|-----------|---------|--------------|
| `dispatch_create()` | Create a dispatch handle for message-driven I/O | ```c<br>dispatch_t *disp = dispatch_create();``` |
| `resmgr_attach()` | Attach the resource manager to a path name (/dev/sensor0) | ```c<br>resmgr_attach(disp, NULL, "/dev/sensor0", _FTYPE_ANY, 0, &operations, &handler);``` |
| `__DIOF / __DIOT / __DIOTF` | Define custom devctl directions: Output, Input, or In/Out | ```c<br>#define SENSOR_DEVCTL_READ_REG __DIOTF(uint32_t, sensor_val)``` |
| `iofunc_notify()` | Support `select()`/`poll()` for multi-client non-blocking I/O | ```c<br>iofunc_notify(default_notify, ocb, 0);``` |
| `thread_pool_create()` | Create a thread pool to handle client requests non-blockingly | ```c<br>pool = thread_pool_create(0, NULL, 0, 0);``` |

### Hands-On Exercise
1. Create dispatch: `disp = dispatch_create();`
2. Attach resource manager to `/dev/sensor0`
3. Open in a second terminal: `cat /dev/sensor0` (blocks until data written)
4. Write telemetry: `echo "42" > /dev/sensor0`
5. Use `select()`/`poll()` to wait for data: `pollfd fds[1] = {{.fd = /dev/sensor0}}; poll(fds, 1, 1000);`

---

#=============================================================================
# SECTION 5: Thread Pool & ham Self-Healing Recovery
#=============================================================================

## 5. Thread Pool & ham Self-Healing Recovery (Zaid)

### Objective
Use `thread_pool_create()` and `iofunc_notify()` (`select()`/`poll()`) for
multi-client non-blocking I/O, plus High Availability Manager (`ham`)
/supervisor recovery so killing the driver (`slay sensor_driver`) triggers
an automatic millisecond restart without an OS reboot.

### QNX-Exclusive Primitives

| Primitive | Purpose | Code Example |
|-----------|---------|--------------|
| `thread_pool_create()` | Create a thread pool for handling multiple client connections | ```c<br>pool = thread_pool_create(0, NULL, 0, 0);``` |
| `ThreadPoolServer()` | Associate the dispatch with the thread pool | ```c<br>ThreadPoolServer(pool, dispatch, 0);``` |
| `iofunc_notify()` | Enable `select()`/`poll()` support on the resource manager | ```c<br>iofunc_notify(default_notify, ocb, 0);``` |
| `ham` (High Availability Manager) | Monitor process; auto-respawn on crash via `slay` | ```bash<br>ham -c ham.conf &<br># ham.conf: <process> sensor_driver <signal> SIGTERM</process>``` |
| `slay` | Kill a process by name (used in testing self-healing) | ```bash<br>slay sensor_driver``` |
| `ham -l` | List monitored processes | ```bash<br>ham -l``` |

### The Self-Healing Cycle (millisecond-scale)

1. **Normal operation**: Driver runs as UID 1000, servicing clients via `/dev/sensor0`
2. **Crash test**: Engineer runs `slay sensor_driver` to simulate a fault
3. **Detection**: `ham` detects the process is gone within ~100 ms
4. **Respwan**: `ham` automatically executes the binary again (marked to run as UID 0 in the config)
5. **Re-initialization**: The new instance starts as UID 0, calls `security_init_hw_privileges()` to re-lock abilities, then `security_drop_root()` to drop to UID 1000
6. **Recovery**: Driver is back online; clients continue reading `/dev/sensor0` without interruption
7. **Video demo**: Split-screen shows `slay sensor_driver` -> `ham` respawn -> `ps -eo pid,user,comm` shows new process

### Hands-On Exercise
1. Start the driver: `/bin/sensor_driver &`
2. Verify it's running: `ps -eo pid,user,comm | grep sensor_driver`
3. Test self-healing: `slay sensor_driver`
4. Watch within 1 second: `ps -eo pid,user,comm | grep sensor_driver` shows new PID
5. Concurrently, watch `poll()` on `/dev/sensor0` reconnect automatically

---

#=============================================================================
# SECTION 6: procmgr_ability Security
#=============================================================================

## 6. procmgr_ability Security & Root-Dropping (Lidiya)

### Objective
Implement least-privilege security using `procmgr_ability()` with
`PROCMGR_AOP_ALLOW | PROCMGR_AOP_LOCK` to allow ONLY the three hardware
abilities (`PROCMGR_AID_IO`, `PROCMGR_AID_INTERRUPT`, `PROCMGR_AID_MEM_PHYS`),
then drop from UID 0 to UID 1000 via `setuid()`/`setgid()`. Log every
transition to `slog2c()`.

### QNX-Exclusive Primitives

| Primitive | Purpose | Code Example |
|-----------|---------|--------------|
| `procmgr_ability()` | Set/lock abilities in the microkernel's ability domain | ```c<br>procmgr_ability(PROCmgr_AID_IO, PROCMGR_AOP_ALLOW | PROCMGR_AOP_LOCK);``` |
| `PROCMGR_AOP_ALLOW` | Allow a specific ability and lock it (permanent) | ```c<br>// Only IO, Interrupt, Mem_Phys allowed; everything else denied``` |
| `PROCMGR_AOP_DENY` | Deny a specific ability and lock it permanent | ```c<br>procmgr_ability(PROCmgr_AID_EOL, PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK);``` |
| `PROCMGR_AID_IO` | Ability to perform I/O port reads/writes (in32/out32) | ```c<br>// Required for user-space MMIO``` |
| `PROCMGR_AID_INTERRUPT` | Ability to attach to hardware interrupts | ```c<br>// Required for InterruptAttachEvent``` |
| `PROCMGR_AID_MEM_PHYS` | Ability to physically map device memory | ```c<br>// Required for mmap_device_memory``` |
| `setuid()/setgid()` | Drop from UID 0 to target non-root UID/GID | ```c<br>setgid(1000); setuid(1000);``` |
| `slog2c()` | Write structured log entries to the QNX slog2 subsystem | ```c<br>slog2c(_SLOG_INFO, 0, "message", 1);``` |

### Security Policy File (bsp/sensor_driver.secpol)

The `sensor_driver.secpol` file defines a dedicated type `sensor_driver_t`
restricted to `/dev/sensor0` and the three hardware abilities. The `ham`
supervisor type (`ham_supervisor_t`) has limited abilities to restart the
driver. When `ham` respawns the binary as UID 0, the cycle repeats
automatically.

```text
type sensor_driver_t;
file /dev/sensor0 { sensor_driver_t };
ability PROCMGR_AID_IO | PROCMGR_AID_INTERRUPT | PROCMGR_AID_MEM_PHYS { sensor_driver_t };
deny { all } unless PROCMGR_AID_IO | PROCMGR_AID_INTERRUPT | PROCMGR_AID_MEM_PHYS;
```

### Hands-On Exercise
1. Run the driver initially as root (or with `sudo`)
2. Observe `security_init_hw_privileges()` lock the three abilities
3. Call `security_drop_root()` and verify `ps -o pid,user` shows UID 1000
4. Attempt an unauthorized operation (e.g., `in32()` without locked ability) -> EPERM
5. Run `pidin -p <pid> user` and `pidin -p <pid> info` to prove locked abilities

### 3-Minute Demo Talking Points (Section 6)
- "Watch as I start the driver: it begins as UID 0, locks down its hardware
abilities, then drops to UID 1000."
- "`pidin -p <pid> user` proves we're running as non-root."
- "`pidin -p <pid> info` shows the locked abilities - notice only the three
  hardware primitives are permitted."
- "If I kill the driver with `slay sensor_driver`, the Ham manager respawns it
  in under 1 second, and the cycle repeats."
- "No kernel modules, no OS reboot - pure microkernel self-healing."

---

#=============================================================================
# SECTION 7: tracelogger Kernel Trace & Stress Profiling
#=============================================================================

## 7. tracelogger Kernel Trace & Stress Profiling

### Objective
Use `tracelogger` in ring/daemon mode to capture both kernel scheduling/interrupt
events and user `TraceEvent` strings to `.kev` files. Run stress benchmarks
with background busy loops. Use `traceprinter` to extract custom user events and
calculate IRQ-to-client latency deltas in microseconds.

### Key Commands & Commands

| Step | Command | Talking Points |
|------|---------|----------------|
| 7.1  | `tracelogger -n 100000 -o /tmp/qnx_quirks_idle.kev -S -C` <br> `  -p 4` | Start tracelogger in ring mode: 100K event buffer, daemon mode, 4procs, slog2 integration. |
| 7.2  | `sleep 5` | 5-second idle baseline period. |
| 7.3  | `for i in $(seq 1 4); do (while : ; do : ; done) & done` | Spawn 4 background busy-loop stress workers (one per core). |
| 7.4  | `sleep 10` | 10-second stress period. |
| 7.5  | `kill $(cat /tmp/qnx_quirks_tracer.pid)` | Cleanly terminate tracelogger. |
| 7.6  | `traceprinter -b /tmp/qnx_quirks_idle.kev -S _NTO_TRACE_USRSTREVENT_CLASS` | Filter to our custom user events only. |
| 7.7  | `traceprinter -b /tmp/qnx_quirks_load.kev -S _NTO_TRACE_USRSTREVENT_CLASS` | Same under load. |
| 7.8  | Compute delta: `(client_cycles - irq_cycles) * 1e9 / cycles_per_sec` | Calculate IRQ-to-client latency: idle ~X µs vs load ~Y µs. |

### 3-Minute Demo Talking Points (Section 7)
- "I'm running the benchmark script: `sh scripts/benchmark_kev.sh`"
- "First it captures 5 seconds of idle trace to `/tmp/qnx_quirks_idle.kev`"
- "Then it spawns background busy loops and captures 10 seconds of loaded trace"
- "`traceprinter` filters our custom events - we can see IRQ pulse events and client delivery events"
- "Under idle conditions, IRQ-to-client latency is ~X microseconds"
- "Under CPU stress, the latency delta shows the microkernel's determinism (or lack thereof)"
- "The `.kev` format is standard QNX kernel trace - analysable with `traceprinter` or external tools"

---

#=============================================================================
# 3-MINUTE LIVE DEMO VIDEO STORYBOARD
#=============================================================================

## Second-by-Second 3-Minute Demo Video Storyboard

The following timeline structures the Devpost Hackathon live demo video.
Each entry includes exact terminal commands, split-screen layouts, and
talking points. The goal is to show all 7 team layers in action within
180 seconds.

| Time (s) | Segment | Split-Screen Layout | Commands & Talking Points |
|----------|---------|---------------------|---------------------------|
| 0:00–0:30 | **Intro & Architecture Overview** | Full-screen title card: "THE QNX QUIRKS - Devpost Hackathon"<br>Bullet points: Microkernel, 7 team members, 5 QNX primitives | Narrator: "Hello! We're Team Lidiya presenting THE QNX QUIRKS - demonstrating why QNX 8.0's microkernel architecture outperforms monolithic kernels for safety-critical systems." |
| 0:30–0:45 | **Section 1: Bring-Up** | Left: `pidin` output showing QNX 8.0 booted<br>Right: `ls -l /bin/dummy_driver` | Command: `procnto -m 256M -c 1G`<br>Talking: "We boot QNX 8.0.5 via `mkqnximage` with SSH enabled. Notice PID 1 is `procnto`, not Linux init - that's the microkernel difference." |
| 0:45–1:15 | **Section 2: MMIO & DMA** | Left: `in32()`/`out32()` code with `#ifdef` split<br>Right: `mmap_device_memory()` call with `PROT_NOCACHE` | Command: `ThreadCtl(_NTO_TCTL_IO, 0);`<br>Talking: "User-space MMIO without kernel drivers. `mmap_device_memory()` with `PROT_NOCACHE` maps physical registers. `#ifdef __aarch64__` for Pi 4, `#ifdef __x86_64__` for QEMU fallback." |
| 1:15–1:45 | **Section 3: Pulse IRQs** | Left: `InterruptAttachEvent()` with `SIGEV_PULSE`<br>Right: `tracelogger` output showing pulse timestamps | Command: `InterruptAttachEvent(veid, event, _PULSE_CODE_UNBLOCK);`<br>Talking: "Aeknath's user-space interrupts. `InterruptAttachEvent()` with `SIGEV_PULSE` wakes a high-priority thread in microseconds - no kernel ISR needed. `InterruptMask()/InterruptUnmask()` control the line." |
| 1:45–2:15 | **Section 4: /dev/sensor0** | Left: `cat /dev/sensor0` reading telemetry<br>Right: `pollfd`/`poll()` waiting for data | Command: `poll(fds, 1, 1000);`<br>Talking: "Siddhant's POSIX Resource Manager. `resmgr_attach()` exposes `open()`/`read()`/`write()`/`devctl()` via `/dev/sensor0`. `iofunc_notify()` enables `select()`/`poll()` for multi-client non-blocking I/O." |
| 2:15–2:30 | **Section 5: Self-Healing** | Left: `ps -eo pid,user,comm` showing UID 1000<br>Right: `slay sensor_driver` -> instant respawn | Command: `slay sensor_driver`<br>Talking: "Zaid's ham recovery. Killing the driver with `slay sensor_driver` triggers automatic millisecond respawn. The new instance starts as UID 0, re-initializes hardware, drops root - all without an OS reboot." |
| 2:30–2:45 | **Section 6: Security** | Left: `pidin -p <pid> user` showing UID 1000<br>Right: `pidin -p <pid> info` showing locked abilities | Command: `pidin -p <pid> user`<br>Talking: "Lidiya's least-privilege security. `procmgr_ability()` locks ONLY the three hardware abilities (`IO`, `Interrupt`, `Mem_Phys`). All others are denied and locked. Then `setuid(1000)` drops privileges." |
| 2:45–3:00 | **Section 7: tracelogger Profiling** | Left: `sh scripts/benchmark_kev.sh` output<br>Right: `traceprinter` filtered results with latency deltas | Command: `sh scripts/benchmark_kev.sh`<br>Talking: "All seven layers profiling. `tracelogger` captures kernel + user TraceEvents to `.kev` files. `traceprinter` extracts our custom events. We calculate IRQ-to-client latency deltas in microseconds: idle vs under CPU stress load." |

### Full Demo Narrative (Flow)

1. **Start**: Boot QNX 8.0, prove driver is running as non-root
2. **MMIO Demo**: Show register reads via user-space MMIO
3. **IRQ Demo**: Trigger a pulse wakeup, show microsecond timing
4. **Sensor Demo**: Read `/dev/sensor0` with `poll()`
5. **Security Proof**: `pidin` shows UID 1000 and locked abilities
6. **Self-Healing**: `slay sensor_driver` -> ham respawn in < 1s
7. **Profiling**: Run `benchmark_kev.sh`, show latency deltas
8. **Close**: "Thank you - QNX 8.0 microkernel technology for safety-critical devices"

### Demo Technical Requirements

| Requirement | Specification |
|-------------|---------------|
| Target hardware | QEMU x86_64 (primary) + Raspberry Pi 4 aarch64le (secondary) |
| QNX version | SDP 8.0.5 |
| Visible terminals | Minimum 3 split-screen panes at any time |
| Logging | All terminal output captured via `script` or video rec |
| Time limit | Strict 180 seconds (3 minutes) |
| Key metrics to show | - UID 1000 execution proof - `pidin` <br> - Locked abilities proof - `pidin info` <br> - IRQ-to-client latency: idle vs load <br> - ham respawn time: < 1 second |

### Devpost Submission Checkboxes

| Award Track | How We Match |
|-------------|--------------|
| **Best Codelab / Tutorial** | Comprehensive, developer-ready Codelab with 7 sections, hands-on exercises, and 3-minute video storyboard. Every team member's contribution is documented with QNX-specific primitives and terminal commands. |
| **Best Use of QNX-Specific Tech** | All 5 QNX-exclusive primitives are demonstrated: user-space MMIO, pulse interrupts, POSIX resource manager, thread pool + ham, procmgr_ability security. Zero Linux-specific headers - strictly `<sys/neutrino.h>`, `<sys/procmgr.h>`, etc. |
| **Best Real-Time System** | `tracelogger`/.keV profiling shows IRQ-to-client latency determinism. `ham` self-healing proves millisecond recovery. Thread pool + `poll()` shows non-blocking multi-client I/O. |
| **Most Innovative Use of Microkernel Architecture** | Zero kernel-space driver code. All five primitives run in user space. Microkernel abilities lockdown via `procmgr_ability()` proves least-privilege enforcement. Self-healing without OS reboot. |

---

#=============================================================================
# QUICK START GUIDE (for Devpost reviewers)
#=============================================================================

## To reproduce the full demo on QEMU x86_64:

```bash
# 1. Build the image
auto_qnx.py compile --src src/driver/dummy_driver_secure.c \
                    --out bin/dummy_driver --arch gcc_ntox86_64
auto_qnx.py deploy --mode daemon

# 2. Verify non-root execution
/prox/boot/dummy_driver &
ps -o pid,user,comm -p $(pgrep dummy_driver)
# Should show UID 1000

# 3. Prove security abilities locked
pidin -p <PID> user
pidin -p <PID> info | grep ability

# 4. Run the KEV benchmark
sh scripts/benchmark_kev.sh

# 5. Test self-healing
slay sensor_driver
# Wait ~1 second, then:
ps -o pid,user,comm -p $(pgrep sensor_driver)
# New process running as UID 1000

# 6. Read telemetry concurrently
cat /dev/sensor0 &
# Should continue reading after slay/respawn
```

## To reproduce on Raspberry Pi 4 aarch64le:

Same commands, but with `--arch gcc_ntoaarch64le` and the BCM2711
MMIO `#ifdef` conditional compilation enabled.

---

=============================================================================
# END OF CODELAB
=============================================================================