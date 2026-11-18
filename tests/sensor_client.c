/*
 * sensor_client.c -- smoke test for /dev/sensor0 (read, write, devctl)
 * Build: qcc -V gcc_ntoaarch64le -Wall -o sensor_client sensor_client.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "driver_interface.h"

int main(void)
{
    int fd = open(SENSOR_DEV_PATH, O_RDWR);
    if (fd < 0) { perror("open " SENSOR_DEV_PATH); return 1; }

    uint32_t inject = 0xCAFE;
    if (write(fd, &inject, sizeof(inject)) != sizeof(inject)) perror("write");

    sensor_sample_t s;
    if (read(fd, &s, sizeof(s)) != sizeof(s)) perror("read");
    else printf("read():   seq=%u value=0x%X t=%llu ns\n",
                s.seq, s.value, (unsigned long long)s.timestamp_ns);

    int rc = devctl(fd, DCMD_SENSOR_GET_SAMPLE, &s, sizeof(s), NULL);
    if (rc != EOK) fprintf(stderr, "devctl GET_SAMPLE: %s\n", strerror(rc));
    else printf("devctl:   seq=%u value=0x%X\n", s.seq, s.value);

    uint32_t hz = 1000;
    rc = devctl(fd, DCMD_SENSOR_SET_RATE, &hz, sizeof(hz), NULL);
    if (rc != EOK) fprintf(stderr, "devctl SET_RATE: %s\n", strerror(rc));

    sensor_status_t st;
    rc = devctl(fd, DCMD_SENSOR_GET_STATUS, &st, sizeof(st), NULL);
    if (rc != EOK) fprintf(stderr, "devctl GET_STATUS: %s\n", strerror(rc));
    else printf("status:   irq=%u overruns=%u rate=%u Hz ver=%u\n",
                st.irq_count, st.overruns, st.rate_hz, st.iface_version);

    close(fd);
    return 0;
}
