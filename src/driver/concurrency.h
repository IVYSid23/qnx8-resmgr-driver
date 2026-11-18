/* concurrency.h -- multithreaded thread pool & iofunc_notify wiring (Zaid) */
#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include <sys/dispatch.h>
#include <sys/iofunc.h>

/* Replaces single-threaded sensor_resmgr_run(dpp) with a QNX thread pool */
int sensor_concurrency_run(dispatch_t *dpp);

/* Initializes the 3-element iofunc_notify_t array for select()/poll() clients */
void sensor_notify_init(iofunc_notify_t notify[3]);

/* Resource manager io_notify callback handler */
int sensor_io_notify(resmgr_context_t *ctp, io_notify_t *msg, RESMGR_OCB_T *ocb, iofunc_notify_t notify[3]);

/* Triggers non-blocking alerts when new sensor data arrives */
void sensor_notify_trigger(iofunc_notify_t notify[3]);

#endif