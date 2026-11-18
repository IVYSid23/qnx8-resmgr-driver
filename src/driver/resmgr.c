/*
 * resmgr.c -- single-threaded POSIX resource manager for /dev/sensor0
 * Owner: Siddhant.
 *
 * Build (QNX SDP 8.0, after `source qnxsdp-env.sh`):
 *   qcc -V gcc_ntoaarch64le -Wall -Iinclude -Isrc/driver -o sensor_resmgr src/driver/resmgr.c src/driver/main_stub.c      (Pi 4/5, QEMU aarch64)
 *   qcc -V gcc_ntox86_64    -Wall -Iinclude -Isrc/driver -o sensor_resmgr src/driver/resmgr.c src/driver/main_stub.c      (QEMU x86_64)
 *
 * Hooks for teammates are marked  >>> HOOK <<<
 *   Basith  : replace hw_read_register()   (real MMIO read)
 *   Aeknath : fill in irq_pulse_handler()  (real InterruptAttachEvent pulses)
 *   Zaid    : swap dispatch loop for thread pool, add iofunc_notify for poll()
 *   Lidiya  : drop root after resmgr_attach() in main()
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#include "driver_interface.h"
#include "resmgr.h"

/* ---- Driver state --------------------------------------------------- */
static sensor_status_t g_status = { 0, 0, 0, SENSOR_IFACE_VERSION };
static uint32_t        g_seq;
static uint32_t        g_sim_value;     /* simulated register (QEMU) */

/* >>> HOOK (Basith): replace with mmap_device_memory() + in32() <<< */
static uint32_t hw_read_register(void)
{
    return g_sim_value;
}

/* >>> HOOK (Aeknath): called when SENSOR_PULSE_IRQ arrives <<< */
static int irq_pulse_handler(message_context_t *ctp, int code,
                             unsigned flags, void *handle)
{
    (void)ctp; (void)code; (void)flags; (void)handle;
    g_status.irq_count++;
    /* InterruptUnmask() goes here once real IRQs are attached. */
    return 0;
}

static void sample_get(sensor_sample_t *s)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    s->seq          = ++g_seq;
    s->value        = hw_read_register();
}

/* ---- Resource manager handlers -------------------------------------- */
static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t      io_funcs;
static iofunc_attr_t          attr;

static int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;

    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    /* Stream-style device: every read() returns one fresh sample. */
    sensor_sample_t s;
    sample_get(&s);

    size_t n = msg->i.nbytes < sizeof(s) ? msg->i.nbytes : sizeof(s);
    if (n == 0) return EOK;

    MsgReply(ctp->rcvid, (int)n, &s, n);
    return _RESMGR_NOREPLY;
}

static int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;

    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    uint32_t v;
    if (msg->i.nbytes != sizeof(v)) return EINVAL;
    if (resmgr_msgread(ctp, &v, sizeof(v), sizeof(msg->i)) != (int)sizeof(v)) return EIO;

    g_sim_value = v;                       /* inject into simulated register */
    _IO_SET_WRITE_NBYTES(ctp, sizeof(v));
    return _RESMGR_NPARTS(0);
}

static int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_devctl_default(ctp, msg, ocb);
    if (status != _RESMGR_DEFAULT) return status;

    void *data  = _DEVCTL_DATA(msg->i);
    int   nbytes = 0;

    switch (msg->i.dcmd) {
    case DCMD_SENSOR_GET_SAMPLE:
        sample_get((sensor_sample_t *)data);
        nbytes = sizeof(sensor_sample_t);
        break;

    case DCMD_SENSOR_GET_STATUS:
        *(sensor_status_t *)data = g_status;
        nbytes = sizeof(sensor_status_t);
        break;

    case DCMD_SENSOR_SET_RATE: {
        uint32_t hz = *(uint32_t *)data;
        if (hz > SENSOR_MAX_RATE_HZ) return EINVAL;
        g_status.rate_hz = hz;             /* >>> HOOK (Aeknath): (re)arm timer/IRQ rate <<< */
        break;
    }

    case DCMD_SENSOR_RESET:
        g_seq = 0;
        g_status.irq_count = 0;
        g_status.overruns  = 0;
        break;

    default:
        return ENOSYS;
    }

    memset(&msg->o, 0, sizeof(msg->o));
    msg->o.ret_val = 0;
    msg->o.nbytes  = nbytes;
    return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + nbytes);
}

/* ---- Public entry points (called from main.c, owned by Lidiya) -------
 *
 *   dispatch_t *dpp = sensor_resmgr_init();   // create /dev/sensor0
 *   ... drop root privileges here (main.c) ...
 *   sensor_resmgr_run(dpp);                   // serve requests forever
 *
 * Zaid: replace the loop in sensor_resmgr_run() with a thread pool
 * (thread_pool_create) and add iofunc_notify for poll()/select().
 */
dispatch_t *sensor_resmgr_init(void)
{
    dispatch_t    *dpp;
    resmgr_attr_t  rattr;

    if ((dpp = dispatch_create()) == NULL) {
        perror("dispatch_create");
        return NULL;
    }

    memset(&rattr, 0, sizeof(rattr));
    rattr.nparts_max   = 1;
    rattr.msg_max_size = 2048;

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_IO_NFUNCS,      &io_funcs);
    io_funcs.read   = io_read;
    io_funcs.write  = io_write;
    io_funcs.devctl = io_devctl;

    iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);

    if (resmgr_attach(dpp, &rattr, SENSOR_DEV_PATH, _FTYPE_ANY, 0,
                      &connect_funcs, &io_funcs, &attr) == -1) {
        perror("resmgr_attach");
        return NULL;
    }

    /* Aeknath's IRQ handler delivers SENSOR_PULSE_IRQ to this dispatch. */
    if (pulse_attach(dpp, MSG_FLAG_ALLOC_PULSE, SENSOR_PULSE_IRQ,
                     irq_pulse_handler, NULL) == -1) {
        perror("pulse_attach");
        return NULL;
    }
    return dpp;
}

int sensor_resmgr_run(dispatch_t *dpp)
{
    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    if (ctp == NULL) { perror("dispatch_context_alloc"); return -1; }

    for (;;) {
        if ((ctp = dispatch_block(ctp)) == NULL) {
            perror("dispatch_block");
            return -1;
        }
        dispatch_handler(ctp);
    }
}
