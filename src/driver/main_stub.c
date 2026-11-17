/* main_stub.c -- TEMPORARY main so Siddhant can test alone.
 * Lidiya's real src/main.c (privilege drop) replaces this. Do not merge both. */
#include <stdlib.h>
#include "resmgr.h"

int main(void)
{
    dispatch_t *dpp = sensor_resmgr_init();
    if (dpp == NULL) return EXIT_FAILURE;
    /* >>> Lidiya: procmgr_ability() + setuid() goes here <<< */
    return sensor_resmgr_run(dpp) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
