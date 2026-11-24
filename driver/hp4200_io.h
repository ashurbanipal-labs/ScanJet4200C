#ifndef HP4200_IO_H
#define HP4200_IO_H

#include <stddef.h>
#include <stdint.h>

struct hp4200_ram_diagnostic;

typedef struct {
    uint64_t total_low_buffer_ms;
    uint64_t longest_low_buffer_ms;
    size_t low_buffer_polls;
    uint8_t last_available_kib;
} hp4200_read_wait_diagnostic;

/* Transport boundary for the HP scan engine. All callbacks return 0 on
 * success and a negative value on failure. A short bulk read is an error. */
typedef struct {
    void *context;
    int (*read_register)(void *context, uint8_t reg, uint8_t *value);
    int (*write_register)(void *context, uint8_t reg, uint8_t value);
    /* PV8630 bridge index write, used by HP's daisy-chain wake sequence. */
    int (*write_bridge)(void *context, uint8_t index, uint8_t value);
    int (*bulk_read)(void *context, uint8_t *data, size_t length);
    int (*bulk_read_ram)(void *context, uint8_t port,
                         uint8_t *data, size_t length);
    int (*bulk_write_ram)(void *context, uint8_t port,
                          const uint8_t *data, size_t length);
    uint64_t (*monotonic_ms)(void *context);
    void (*sleep_ms)(void *context, unsigned milliseconds);
    int (*cancelled)(void *context);
    /* Optional per-session counter for final RAM planes that needed a
     * verified rewrite after the USB write reported success. */
    unsigned *verified_plane_rewrites;
    /* Optional details of a final shading or lookup upload failure. */
    struct hp4200_ram_diagnostic *final_ram_diagnostic;
    /* Optional status trace for diagnosing an image-data stall. */
    hp4200_read_wait_diagnostic *read_wait_diagnostic;
    /* Optional diagnostic of raw image rows before sample selection, RGB
     * alignment, and color conversion. A callback failure disables capture
     * but does not interrupt the scan. */
    void *raw_observer_context;
    int (*observe_raw_line)(void *context, size_t row,
                            const uint8_t *data, size_t length);
    /* Zero selects HP's 8 MiB host-buffer cap. Larger caps are experimental
     * performance settings; scanner register timing is unchanged. */
    size_t host_buffer_limit;
} hp4200_io;

/* hpad32.dll FUN_10006a70: poll LM9830 register 1, retain 1 KiB in SRAM,
 * and fail after 15 seconds without forward progress. */
int hp4200_read_buffer(const hp4200_io *io, uint8_t *data, size_t length);

/* Stop the scan motor on explicit cancellation or error. */
int hp4200_stop(const hp4200_io *io);

#endif
