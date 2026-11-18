/* concurrency.c -- QNX 8.0 thread_pool_create & iofunc_notify module (Zaid) */
#include "concurrency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int sensor_concurrency_run(dispatch_t *dpp) {
    thread_pool_attr_t pool_attr;
    thread_pool_t *tpp;

    if (dpp == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&pool_attr, 0, sizeof(pool_attr));
    pool_attr.handle = dpp;
    pool_attr.context_alloc = (void *)dispatch_context_alloc;
    pool_attr.block_func    = (void *)dispatch_block;
    pool_attr.unblock_func  = (void *)dispatch_unblock;
    pool_attr.handler_func  = (void *)dispatch_handler;
    pool_attr.context_free  = (void *)dispatch_context_free;

    /* Worker pool sizing: 2 idle workers min, 4 max idle, 8 max concurrent clients */
    pool_attr.lo_water  = 2;
    pool_attr.hi_water  = 4;
    pool_attr.increment = 1;
    pool_attr.maximum   = 8;

    tpp = thread_pool_create(&pool_attr, POOL_FLAG_EXIT_SELF);
    if (tpp == NULL) {
        perror("thread_pool_create failed");
        return -1;
    }

    /* Starts worker threads and blocks forever handling concurrent client requests */
    return thread_pool_start(tpp);
}

void sensor_notify_init(iofunc_notify_t notify[3]) {
    if (notify != NULL) {
        IOFUNC_NOTIFY_INIT(notify);
    }
}

int sensor_io_notify(resmgr_context_t *ctp, io_notify_t *msg, RESMGR_OCB_T *ocb, iofunc_notify_t notify[3]) {
    int trig = 0;

    /* Report output ready and input ready when sensor data is available */
    trig |= _NOTIFY_COND_OUTPUT;
    trig |= _NOTIFY_COND_INPUT;

    return iofunc_notify(ctp, msg, notify, trig, NULL, NULL);
}

void sensor_notify_trigger(iofunc_notify_t notify[3]) {
    if (notify != NULL) {
        iofunc_notify_trigger(notify, 1, IOFUNC_NOTIFY_INPUT);
    }
}