#include <stdio.h>
#include <unistd.h>
#include <sys/slog2.h>

int main(void) {
    slog2_buffer_set_config_t buffer_config;
    slog2_buffer_t buffer_handle;

    buffer_config.buffer_set_name = "dummy_driver";
    buffer_config.num_buffers = 1;
    buffer_config.verbosity_level = SLOG2_INFO;
    buffer_config.buffer_config[0].buffer_name = "default";
    buffer_config.buffer_config[0].num_pages = 1;

    if (slog2_register(&buffer_config, &buffer_handle, 0) == -1) {
        return 1;
    }

    int count = 0;
    while (1) {
        slog2f(buffer_handle, 0, SLOG2_INFO, "dummy_driver heartbeat #%d", count++);
        sleep(5);
    }

    return 0;
}