#include "hp4200_origin_scan.h"
#include "hp4200_home.h"
#include "hp4200_motor.h"
#include "hp4200_origin.h"

#include <stdlib.h>

int hp4200_measure_origin(const hp4200_io *io,
                          const hp4200_register_bank *factory,
                          uint16_t initial_y, uint16_t *origin_x,
                          uint16_t *origin_y)
{
    uint8_t *image;
    unsigned start, end, factory_x;
    size_t stride, length;
    int result = -1, motor_running = 0, returning = 0;
    if (!io || !io->read_register || !io->write_register || !io->bulk_read ||
        !io->monotonic_ms || !io->sleep_ms || !factory ||
        !origin_x || !origin_y) return -1;
    start = ((unsigned)factory->value[0x22] << 8) | factory->value[0x23];
    end = ((unsigned)factory->value[0x24] << 8) | factory->value[0x25];
    factory_x = ((unsigned)factory->value[0x1e] << 8) | factory->value[0x1f];
    if (end <= start || ((end - start) & 1) || end - start > 0x2000)
        return -1;
    stride = ((end - start) / 2) * 3 + 1;
    length = stride * 150;
    image = malloc(length);
    if (!image) return -1;
    if (hp4200_home_wait_clear(io)) goto done;
    /* FUN_10005c40, followed by reg 7 = 3. */
    if (io->write_register(io->context, 7, 8) ||
        io->write_register(io->context, 7, 0)) goto done;
    io->sleep_ms(io->context, 50);
    if (io->cancelled && io->cancelled(io->context)) goto done;
    if (io->write_register(io->context, 7, 3)) goto done;
    motor_running = 1;
    if (hp4200_read_buffer(io, image, length)) goto done;
    if (hp4200_motor_finish(io)) goto done;
    motor_running = 0;
    returning = 1;
    result = hp4200_find_origin(image, stride, 150, (uint16_t)factory_x,
                                 initial_y, origin_x, origin_y);
done:
    if (motor_running) {
        if (hp4200_motor_finish(io) == 0) returning = 1;
    }
    if (result && !returning) hp4200_stop(io);
    free(image);
    return result;
}
