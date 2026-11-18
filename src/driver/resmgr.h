/* resmgr.h -- public entry points of the /dev/sensor0 resource manager (Siddhant) */
#ifndef RESMGR_H
#define RESMGR_H
#include <sys/dispatch.h>

dispatch_t *sensor_resmgr_init(void);        /* NULL on failure */
int         sensor_resmgr_run(dispatch_t *); /* blocks forever; -1 on error */

#endif
