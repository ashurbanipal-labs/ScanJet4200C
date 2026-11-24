#include "hp4200_motor.h"

int hp4200_motor_start(const hp4200_io *io)
{
    uint8_t reg58;
    if (!io || !io->read_register || !io->write_register || !io->sleep_ms)
        return -1;
    if (io->cancelled && io->cancelled(io->context)) return -2;
    if (io->read_register(io->context, 0x58, &reg58) ||
        io->write_register(io->context, 0x58, reg58 & 0xdf) ||
        io->write_register(io->context, 0x07, 0x08) ||
        io->write_register(io->context, 0x07, 0x00)) goto failure;
    io->sleep_ms(io->context, 50);
    if (io->cancelled && io->cancelled(io->context)) goto failure;
    if (io->write_register(io->context, 0x07, 0x03)) goto failure;
    return 0;
failure:
    hp4200_stop(io);
    return -1;
}

int hp4200_motor_finish(const hp4200_io *io)
{
    uint8_t reg58;
    if (!io || !io->read_register || !io->write_register || !io->sleep_ms)
        return -1;
    if (io->read_register(io->context, 0x58, &reg58) ||
        io->write_register(io->context, 0x58, reg58 | 0x20) ||
        io->write_register(io->context, 0x07, 0x00)) {
        hp4200_stop(io);
        return -1;
    }
    io->sleep_ms(io->context, 50);
    if (io->write_register(io->context, 0x07, 0x02)) {
        hp4200_stop(io);
        return -1;
    }
    return 0;
}
