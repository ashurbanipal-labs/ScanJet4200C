#include "hp4200_io.h"

#include <stdint.h>

static void finish_low_buffer_wait(const hp4200_io *io, uint64_t started_ms,
                                   uint64_t ended_ms)
{
    hp4200_read_wait_diagnostic *trace = io->read_wait_diagnostic;
    uint64_t waited = ended_ms - started_ms;
    trace->total_low_buffer_ms += waited;
    if (waited > trace->longest_low_buffer_ms)
        trace->longest_low_buffer_ms = waited;
}

int hp4200_stop(const hp4200_io *io)
{
    if (!io || !io->write_register) return -1;
    return io->write_register(io->context, 0x07, 0x00);
}

int hp4200_read_buffer(const hp4200_io *io, uint8_t *data, size_t length)
{
    uint64_t progress_ms;
    uint64_t low_buffer_started_ms = 0;
    int waiting_for_buffer = 0;
    if (!io || !io->read_register || !io->bulk_read || !io->monotonic_ms ||
        (!data && length)) return -1;
    progress_ms = io->monotonic_ms(io->context);
    while (length) {
        uint8_t available_kib;
        size_t count;
        uint64_t now;
        if (io->cancelled && io->cancelled(io->context)) {
            if (waiting_for_buffer)
                finish_low_buffer_wait(io, low_buffer_started_ms,
                                       io->monotonic_ms(io->context));
            return -2;
        }
        if (io->read_register(io->context, 0x01, &available_kib)) {
            if (waiting_for_buffer)
                finish_low_buffer_wait(io, low_buffer_started_ms,
                                       io->monotonic_ms(io->context));
            return -3;
        }
        if (io->read_wait_diagnostic)
            io->read_wait_diagnostic->last_available_kib = available_kib;
        if (available_kib < 2) {
            now = io->monotonic_ms(io->context);
            if (io->read_wait_diagnostic) {
                hp4200_read_wait_diagnostic *trace =
                    io->read_wait_diagnostic;
                ++trace->low_buffer_polls;
                if (!waiting_for_buffer) {
                    low_buffer_started_ms = now;
                    waiting_for_buffer = 1;
                }
                uint64_t waited = now - low_buffer_started_ms;
                if (waited > trace->longest_low_buffer_ms)
                    trace->longest_low_buffer_ms = waited;
            }
            if (now - progress_ms > 15000) {
                if (waiting_for_buffer)
                    finish_low_buffer_wait(io, low_buffer_started_ms, now);
                return -4;
            }
            continue;
        }
        if (waiting_for_buffer) {
            now = io->monotonic_ms(io->context);
            finish_low_buffer_wait(io, low_buffer_started_ms, now);
            waiting_for_buffer = 0;
        }
        count = ((size_t)available_kib - 1) * 1024;
        if (count > length) count = length;
        /* The HP Windows bulk primitive splits reads at 65,535 bytes. */
        if (count > 65535) count = 65535;
        if (io->bulk_read(io->context, data, count)) return -5;
        data += count;
        length -= count;
        progress_ms = io->monotonic_ms(io->context);
    }
    return 0;
}
