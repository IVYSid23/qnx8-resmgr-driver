/*=============================================================================
 *  perf_trace.h - Microsecond Latency Instrumentation
 *  QNX SDP 8.0 - THE QNX QUIRKS - Team: Member 7 (Lidiya)
 *===========================================================================*/
#ifndef PERF_TRACE_H_INCLUDED
#define PERF_TRACE_H_INCLUDED

#include <sys/neutrino.h>
#include <sys/page.h>  /* for syspage */
#include <trace.h>
#include <slog2.h>
#include <stdint.h>

/*
 * TRACE_STAMP_IRQ_PULSE - Record an IRQ pulse event sequence number.
 * Use: TRACE_STAMP_IRQ_PULSE(seq_id)
 *      seq_id: unique sequence identifier for the pulse wakeup
 * Context: Aeknath's pulse-driven IRQ wakeup path
 */
#define TRACE_STAMP_IRQ_PULSE(seq_id) do { \
    uint64_t __ts_cycles = ClockCycles(); \
    traceusrinfopulse_((seq_id), __ts_cycles, __LINE__, __FILE__); \
} while(0)

/*
 * TRACE_STAMP_MMIO_DONE - Record MMIO completion event with register value.
 * Use: TRACE_STAMP_MMIO_DONE(seq_id, reg_val)
 *      seq_id: unique sequence identifier for the MMIO operation
 *      reg_val: the register value read/written
 * Context: Basith's user-space MMIO/DMA completion
 */
#define TRACE_STAMP_MMIO_DONE(seq_id, reg_val) do { \
    uint64_t __ts_cycles = ClockCycles(); \
    traceusrstremmio_((seq_id), (reg_val), __ts_cycles, __LINE__, __FILE__); \
} while(0)

/*
 * TRACE_STAMP_CLIENT_DELIVERED - Record client data delivery handoff.
 * Use: TRACE_STAMP_CLIENT_DELIVERED(seq_id)
 *      seq_id: unique sequence identifier for the client delivery
 * Context: Zaid's iofunc_notify / I/O pipeline handoff
 */
#define TRACE_STAMP_CLIENT_DELIVERED(seq_id) do { \
    uint64_t __ts_cycles = ClockCycles(); \
    traceusrstrecv_((seq_id), __ts_cycles, __LINE__, __FILE__); \
} while(0)

/*
 * cycles_to_ns - Convert clock cycle delta to nanoseconds.
 * Uses the global cycles_per_sec from the QNX syspage (qtime).
 * Performance: zero-overhead inline, evaluated at compile time when
 *   cycles_per_sec is a compile-time constant (which it is on QNX 8.0).
 *
 * Use: uint64_t ns = cycles_to_ns(delta_cycles);
 */
static inline uint64_t cycles_to_ns(uint64_t delta_cycles)
{
    /* Get cycles_per_sec from the system page.
     * SYSPAGE_ENTRY is evaluated at link time against the running kernel. */
    uint64_t cycles_per_sec = (uint64_t)
        SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    if (cycles_per_sec == 0) {
        return 0; /* Avoid division by zero */
    }

    /* ns = (delta_cycles * 1_000_000_000) / cycles_per_sec */
    /* Use unsigned 128-bit arithmetic implicitly via uint64_t chain:
     * multiply first, then divide. The intermediate product is */
    /* calculated in 64-bit; if overflow is a concern, the caller */
    /* should ensure delta_cycles < cycles_per_sec / 1e9. */
    return (delta_cycles * 1000000000ULL) / cycles_per_sec;
}

/*=============================================================================
 * Internal trace event definitions (usrapp tracing)
 * These map to _NTO_TRACE_INSERTUSRSTREVENT with subclass codes.
 *===========================================================================*/

/* IRQ pulse subclass code (user-defined but documented) */
#define USRSTREVENT_SUBCLASS_IRQ_PULSE  ((_USRSTREVENT_SUBCLASS_T)0x01)

/* MMIO done subclass code */
#define USRSTREVENT_SUBCLASS_MMIO_DONE   ((_USRSTREVENT_SUBCLASS_T)0x02)

/* Client delivered subclass code */
#define USRSTREVENT_SUBCLASS_CLIENT_DELIVERED  ((_USRSTREVENT_SUBCLASS_T)0x03)

/*
 * traceusrinfopulse - Internal helper: emit a pulse-style usrstrevent.
 * Maps to TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, ...)
 *  - class: _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS)
 *  - subclass: USRSTREVENT_SUBCLASS_IRQ_PULSE
 *  - payload: seq_id (uint64_t), cycles (uint64_t), line (int), file (const char*)
 */
static inline void traceusrinfopulse(uint16_t seq_id, uint64_t cycles)
{
    /* Build the payload as a single uint64_t payload for the trace event.
     * The TraceEvent API takes variadic arguments; we pack seq_id and cycles
     * into the 2 u64 arguments (arg0 and arg1). */
    int retval = TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT,
        _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS),
        USRSTREVENT_SUBCLASS_IRQ_PULSE,
        (uint64_t)seq_id,          /* arg0 = seq_id */
        cycles,                    /* arg1 = timestamp_cycles */
        (uint64_t)__LINE__,        /* arg2 = source line */
        (uint64_t)(uintptr_t)__FILE__); /* arg3 = source file ptr */
    (void)retval; /* suppress unused warning */
}

/*
 * traceusrstremmio - Internal helper: emit an MMIO usrstrevent.
 * Maps to TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, ...)
 *  - class: _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS)
 *  - subclass: USRSTREVENT_SUBCLASS_MMIO_DONE
 *  - payload: seq_id (uint64_t), reg_val (uint64_t), cycles (uint64_t), line (int), file (const char*)
 */
static inline void traceusrstremmio(uint16_t seq_id, uint64_t reg_val)
{
    int retval = TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT,
        _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS),
        USRSTREVENT_SUBCLASS_MMIO_DONE,
        (uint64_t)seq_id,          /* arg0 = seq_id */
        reg_val,                   /* arg1 = register value */
        (uint64_t)ClockCycles(),   /* arg2 = timestamp_cycles */
        (uint64_t)__LINE__,        /* arg3 = source line */
        (uint64_t)(uintptr_t)__FILE__); /* arg4 = source file ptr */
    (void)retval;
}

/*
 * traceusrstrecv - Internal helper: emit a client-delivered usrstrevent.
 * Maps to TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, ...)
 *  - class: _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS)
 *  - subclass: USRSTREVENT_SUBCLASS_CLIENT_DELIVERED
 *  - payload: seq_id (uint64_t), cycles (uint64_t), line (int), file (const char*)
 */
static inline void traceusrstrecv(uint16_t seq_id, uint64_t cycles)
{
    int retval = TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT,
        _SUBSCRIPTION_CLASS(_NTO_TRACE_USRSTREVENT_CLASS),
        USRSTREVENT_SUBCLASS_CLIENT_DELIVERED,
        (uint64_t)seq_id,          /* arg0 = seq_id */
        cycles,                    /* arg1 = timestamp_cycles */
        (uint64_t)__LINE__,        /* arg2 = source line */
        (uint64_t)(uintptr_t)__FILE__); /* arg3 = source file ptr */
    (void)retval;
}

#endif /* !defined(PERF_TRACE_H_INCLUDED) */