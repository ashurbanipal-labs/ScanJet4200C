#include "hp4200_home.h"

static int restore_or_stop(const hp4200_io *io, uint8_t reg58, int status)
{
    if (io->write_register(io->context, 0x58, reg58)) status = -1;
    if (status) hp4200_stop(io);
    return status;
}

int hp4200_home_start(const hp4200_io *io)
{
    uint8_t saved, status;
    if (!io || !io->read_register || !io->write_register || !io->sleep_ms)
        return -1;
    if (io->cancelled && io->cancelled(io->context)) return -2;
    if (io->read_register(io->context, 0x58, &saved)) return -1;
    if (io->write_register(io->context, 0x58, saved & 0xef) ||
        io->read_register(io->context, 0x02, &status))
        return restore_or_stop(io, saved, -1);
    if (io->write_register(io->context, 0x58, saved))
        return restore_or_stop(io, saved, -1);
    if ((status & 2) == 0) {
        if (io->cancelled && io->cancelled(io->context))
            return restore_or_stop(io, saved, -2);
        io->sleep_ms(io->context, 50);
        if (io->cancelled && io->cancelled(io->context))
            return restore_or_stop(io, saved, -2);
        if (io->write_register(io->context, 0x07, 0x02))
            return restore_or_stop(io, saved, -1);
    }
    return 0;
}

static int wait_sensor(const hp4200_io *io, int want_set,
                       unsigned timeout_ms)
{
    uint64_t started = io->monotonic_ms(io->context);
    for (;;) {
        uint8_t status;
        if (io->cancelled && io->cancelled(io->context)) return -2;
        if (io->read_register(io->context, 0x02, &status)) return -1;
        if (((status & 2) != 0) == want_set) return 0;
        if (io->monotonic_ms(io->context) - started >= timeout_ms) return -3;
        if (io->sleep_ms) io->sleep_ms(io->context, 10);
    }
}

int hp4200_home_wait_clear(const hp4200_io *io)
{
    uint8_t saved;
    int status;
    if (!io || !io->read_register || !io->write_register ||
        !io->monotonic_ms || !io->sleep_ms) return -1;
    if (io->read_register(io->context, 0x58, &saved)) return -1;
    if (io->write_register(io->context, 0x58, saved & 0xef))
        return restore_or_stop(io, saved, -1);
    status = wait_sensor(io, 1, 15000);
    if (status) return restore_or_stop(io, saved, status);
    io->sleep_ms(io->context, 50);
    if (io->write_register(io->context, 0x07, 0x01))
        return restore_or_stop(io, saved, -1);
    io->sleep_ms(io->context, 50);
    status = wait_sensor(io, 0, 2000);
    return restore_or_stop(io, saved, status);
}

int hp4200_lamp_on(const hp4200_io *io)
{
    static const uint8_t values[4] = {0x3f, 0xff, 0, 1};
    if (!io || !io->write_register) return -1;
    for (unsigned i = 0; i < 4; ++i)
        if (io->write_register(io->context, (uint8_t)(0x2c + i), values[i]))
            return -1;
    return 0;
}

int hp4200_lamp_off(const hp4200_io *io)
{
    static const uint8_t values[4] = {0, 1, 0x3f, 0xff};
    uint8_t reg69;
    if (!io || !io->read_register || !io->write_register) return -1;
    for (unsigned i = 0; i < 4; ++i)
        if (io->write_register(io->context, (uint8_t)(0x2c + i), values[i]))
            return -1;
    if (io->read_register(io->context, 0x69, &reg69)) return -1;
    return io->write_register(io->context, 0x69, reg69 & 0xf7);
}
