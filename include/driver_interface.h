/*
 * driver_interface.h  --  THE contract between driver, clients, tests, demo.
 * Owner: Siddhant.  Status: v1 (DRAFT -> freeze at hour ~8, changes via Siddhant only)
 *
 * Everyone codes against this file: Aeknath (pulse), Basith (MMIO),
 * Zaid (concurrency/notify), Lidiya (benchmarks), Benson (.build launch).
 */
#ifndef DRIVER_INTERFACE_H
#define DRIVER_INTERFACE_H

#include <stdint.h>
#include <devctl.h>
#include <sys/neutrino.h>   /* _PULSE_CODE_MINAVAIL */

#define SENSOR_IFACE_VERSION   1

/* ---- Device path ---------------------------------------------------- */
#define SENSOR_DEV_PATH        "/dev/sensor0"

/* ---- Data types (fixed width, no padding surprises) ----------------- */
typedef struct {
    uint64_t timestamp_ns;   /* CLOCK_MONOTONIC at sample time          */
    uint32_t seq;            /* increments per sample                   */
    uint32_t value;          /* raw sensor/register value               */
} sensor_sample_t;           /* 16 bytes; read() returns exactly one    */

typedef struct {
    uint32_t irq_count;      /* interrupts (pulses) handled             */
    uint32_t overruns;       /* samples dropped / missed                */
    uint32_t rate_hz;        /* current sample rate (0 = on demand)     */
    uint32_t iface_version;  /* SENSOR_IFACE_VERSION                    */
} sensor_status_t;

/* ---- devctl() commands ---------------------------------------------
 * __DIOF  = data flows driver -> client
 * __DIOT  = data flows client -> driver
 * __DION  = no data
 */
#define DCMD_SENSOR_GET_SAMPLE  __DIOF(_DCMD_MISC, 0x01, sensor_sample_t)
#define DCMD_SENSOR_GET_STATUS  __DIOF(_DCMD_MISC, 0x02, sensor_status_t)
#define DCMD_SENSOR_SET_RATE    __DIOT(_DCMD_MISC, 0x03, uint32_t)      /* Hz, 0..SENSOR_MAX_RATE_HZ */
#define DCMD_SENSOR_RESET       __DION(_DCMD_MISC, 0x04)

#define SENSOR_MAX_RATE_HZ      10000u

/* ---- write() semantics ---------------------------------------------
 * write(fd, &uint32_t, 4): inject a value into the simulated register.
 * Used on QEMU where there is no real sensor. Ignored once real HW is wired.
 */

/* ---- Pulse codes (driver-internal, Aeknath) ------------------------- */
#define SENSOR_PULSE_IRQ        (_PULSE_CODE_MINAVAIL + 1)   /* hardware IRQ fired     */
#define SENSOR_PULSE_TIMER      (_PULSE_CODE_MINAVAIL + 2)   /* simulated-sample tick  */

#endif /* DRIVER_INTERFACE_H */
